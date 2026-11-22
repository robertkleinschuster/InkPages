// Undo and redo bring strokes back as copies (a stroke cannot be added again); edits recorded earlier still name the
// original and must find the copy (InkState::Current) - with Windows ink's real container and strokes.
#include <gtest/gtest.h>

#include <memory>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "ink/StrokeEdit.h"
#include "ink/Strokes.h"
#include "tools/InkSelection.h"

namespace inkpages::tests
{
    using namespace detail;

    struct StrokeEditTest : ::testing::Test
    {
        InkState ink;
        void SetUp() override
        {
            ink.container = winrt::Windows::UI::Input::Inking::InkStrokeContainer();
            ink.layout = PageLayout({ { 600, 800 } });
        }
        InkStroke Line()
        {
            return MakeStroke({ { { 100, 100 } }, { { 200, 110 } }, { { 300, 120 } } }, InkDrawingAttributes());
        }
        uint32_t Count() const { return ink.container.GetStrokes().Size(); }
    };

    TEST_F(StrokeEditTest, DrawEraseUndoUndoRedoRedo)
    {
        InkStroke s = Line();
        ink.Add(s, 0);
        auto draw = std::make_shared<StrokeEdit>(ink);
        draw->Added(s, 0);
        ink.Remove({ s });
        auto erase = std::make_shared<StrokeEdit>(ink);
        erase->Removed(s, 0);
        ASSERT_EQ(0u, Count());
        erase->Undo();
        EXPECT_EQ(1u, Count()) << "the erased stroke is back";
        draw->Undo();
        EXPECT_EQ(0u, Count()) << "undoing the drawing takes the copy undo brought back";
        draw->Redo();
        EXPECT_EQ(1u, Count());
        erase->Redo();
        EXPECT_EQ(0u, Count()) << "redoing the erase takes the newest copy, no duplicate is left";
        erase->Undo();
        EXPECT_EQ(1u, Count());
    }

    TEST_F(StrokeEditTest, DrawMoveUndoUndoRedoRedo)
    {
        InkStroke s = Line();
        ink.Add(s, 0);
        auto draw = std::make_shared<StrokeEdit>(ink);
        draw->Added(s, 0);
        InkStroke moved = Transformed(s, Affine::Translation(50, 50));
        ink.Remove({ s });
        ink.Add(moved, 0);
        auto move = std::make_shared<StrokeEdit>(ink);
        move->Removed(s, 0);
        move->Added(moved, 0);
        move->Undo();
        EXPECT_EQ(1u, Count());
        draw->Undo();
        EXPECT_EQ(0u, Count());
        draw->Redo();
        EXPECT_EQ(1u, Count());
        move->Redo();
        ASSERT_EQ(1u, Count()) << "the moved stroke replaces the drawn one";
        EXPECT_GT(BoundsOf(ink.container.GetStrokes().GetAt(0)).left, 140.f);
    }

    TEST_F(StrokeEditTest, TouchesFindsInkNearAPoint)
    {
        EXPECT_FALSE(ink.Touches({ 150, 105 }, 2)) << "no ink yet";
        ink.Add(Line(), 0);
        EXPECT_TRUE(ink.Touches({ 150, 105 }, 2)) << "on the line";
        EXPECT_TRUE(ink.Touches({ 150, 107.5f }, 2)) << "within the radius plus half the drawn width";
        EXPECT_FALSE(ink.Touches({ 150, 130 }, 2)) << "beside the line";
        EXPECT_FALSE(ink.Touches({ 400, 120 }, 2)) << "beyond its end";
    }

    TEST_F(StrokeEditTest, TranslationPreviewMovesOnlyTheSelectionAndRestoresItOnCancel)
    {
        auto selected = Line();
        auto other = Line();
        ink.Add(selected, 0);
        ink.Add(other, 1);
        InkSelection selection(ink);
        selection.SelectAll(0);
        selection.Preview(Affine::Translation(30, 20));
        selection.Preview(Affine::Translation(-5, 8));
        EXPECT_FLOAT_EQ(-5, selected.PointTransform().m31);
        EXPECT_FLOAT_EQ(8, selected.PointTransform().m32);
        EXPECT_FLOAT_EQ(0, other.PointTransform().m31);

        selection.EndPreview();
        EXPECT_FLOAT_EQ(0, selected.PointTransform().m31);
        EXPECT_FLOAT_EQ(0, selected.PointTransform().m32);
        EXPECT_FALSE(selected.Selected());
        EXPECT_FALSE(other.Selected());
    }
}
