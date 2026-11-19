#include <gtest/gtest.h>

#include "inkpages/model/Pens.h"
#include "inkpages/model/StrokeEraser.h"
#include "inkpages/model/UndoHistory.h"

namespace inkpages::tests
{
    // ---- PenWidths, InkWidth (ported from PenWidthsTests / SavePointTests)

    TEST(PenWidths, SavedWidthsGoToTheNearestStep)
    {
        EXPECT_EQ(1, PenWidths::StepOf(1.0f));
        EXPECT_EQ(2, PenWidths::StepOf(1.41f));
        EXPECT_EQ(0, PenWidths::StepOf(0.3f));
        EXPECT_EQ(5, PenWidths::StepOf(10));
        for (int step = 0; step < static_cast<int>(PenWidths::Steps.size()); ++step)
        {
            EXPECT_EQ(step, PenWidths::StepOf(PenWidths::Steps[step])) << "every step round-trips";
        }
        EXPECT_EQ(1, HighlighterWidths::StepOf(ToolChoices::DefaultHighlighterWidth));
    }

    TEST(InkWidth, PressureForIsTheInverseOfTheFactor)
    {
        for (float p : { 0.f, 0.5f, 1.f }) EXPECT_NEAR(p, InkWidth::PressureFor(InkWidth::Factor(p)), 1e-6);
        EXPECT_FLOAT_EQ(1.75f, InkWidth::Factor(1));
        EXPECT_FLOAT_EQ(2.f, InkWidth::Drawn(2, false, 0.5f)) << "size s at half pressure";
        EXPECT_FLOAT_EQ(2.f, InkWidth::Drawn(2, true, 1)) << "no pressure: its size";
    }

    // ---- ToolChoices (ported from ToolChoicesTests; JSON there, a line of text here)

    TEST(ToolChoices, EmptyPresetsMeanTheDefaults)
    {
        ToolChoices choices;
        EXPECT_EQ(ToolChoices::DefaultPens(), choices.PensOrDefault());
        EXPECT_EQ(ToolChoices::DefaultHighlighters(), choices.HighlightersOrDefault());
        EXPECT_EQ(2, choices.PensOrDefault()[2].color) << "black, blue, red";
    }

    TEST(ToolChoices, SavedPresetsWin)
    {
        ToolChoices choices;
        choices.pens = { { 4, 0.85f } };
        EXPECT_EQ((std::vector<PenPreset>{ { 4, 0.85f } }), choices.PensOrDefault());
        EXPECT_EQ(ToolChoices::DefaultHighlighters(), choices.HighlightersOrDefault());
    }

    TEST(ToolChoices, RoundTripsThroughText)
    {
        ToolChoices choices;
        choices.pens = { { 1, 2.26f }, { 9, 0.42f } };
        choices.highlighters = { { 2, 12 } };
        choices.eraserRadius = 7.5f;
        choices.wholeStrokeErasing = true;
        choices.tool = Tool::Highlighter;
        choices.preset = 1;
        ToolChoices back = ToolChoices::Parse(choices.Serialize());
        EXPECT_EQ(choices.pens, back.pens);
        EXPECT_EQ(choices.highlighters, back.highlighters);
        EXPECT_EQ(7.5f, back.eraserRadius);
        EXPECT_TRUE(back.wholeStrokeErasing);
        EXPECT_EQ(Tool::Highlighter, back.tool);
        EXPECT_EQ(1, back.preset);
    }

    TEST(ToolChoices, MissingOrDamagedFieldsKeepTheirDefaults)
    {
        ToolChoices back = ToolChoices::Parse("");
        EXPECT_TRUE(back.pens.empty());
        EXPECT_EQ(4, back.eraserRadius);
        EXPECT_FALSE(back.wholeStrokeErasing);
        ToolChoices damaged = ToolChoices::Parse("pens=0:1.41,99:2;eraser=abc;tool=7;whole=1");
        EXPECT_TRUE(damaged.pens.empty()) << "a color outside the palette: the defaults";
        EXPECT_EQ(4, damaged.eraserRadius);
        EXPECT_EQ(Tool::Pen, damaged.tool);
        EXPECT_TRUE(damaged.wholeStrokeErasing);
    }

    // ---- StrokeEraser (ported from StrokeEraserTests)

    std::vector<InkSample> Line(std::initializer_list<float> xy)
    {
        std::vector<InkSample> samples;
        for (auto it = xy.begin(); it != xy.end(); it += 2) samples.push_back({ { *it, *(it + 1) }, 0.5f });
        return samples;
    }
    double Half(InkSample const&) { return 1; } // a stroke 2 pt wide

    TEST(StrokeEraser, CircleInTheMiddleCutsTheStrokeWhereTheInkEdgeMeetsIt)
    {
        // The ink edge (half width 1) stops at the circle: the centerline ends 5 + 1 = 6 away from the center.
        auto pieces = *EraseCircle(Line({ 0, 0, 100, 0 }), Half, { 50, 0 }, 5);
        ASSERT_EQ(2u, pieces.size());
        EXPECT_NEAR(44, pieces[0].back().position.x, 1e-4);
        EXPECT_NEAR(56, pieces[1].front().position.x, 1e-4);
        EXPECT_EQ((PointF{ 0, 0 }), pieces[0].front().position);
        EXPECT_EQ((PointF{ 100, 0 }), pieces[1].back().position);
    }

    TEST(StrokeEraser, InkJustOutsideTheCircleStays)
    {
        EXPECT_FALSE(EraseCircle(Line({ -20, 6, 20, 6 }), Half, { 0, 0 }, 5).has_value());
        EXPECT_TRUE(EraseCircle(Line({ -20, 5.9f, 20, 5.9f }), Half, { 0, 0 }, 5).has_value());
    }

    TEST(StrokeEraser, ErasingAnEndShortensAndErasingAllRemovesIt)
    {
        auto shortened = *EraseCircle(Line({ 0, 0, 10, 0, 20, 0 }), Half, { 0, 0 }, 4);
        ASSERT_EQ(1u, shortened.size());
        EXPECT_NEAR(5, shortened[0].front().position.x, 1e-4);
        EXPECT_TRUE(EraseCircle(Line({ 0, 0, 3, 0 }), Half, { 1, 0 }, 10)->empty());
    }

    TEST(StrokeEraser, PressureIsInterpolatedAtTheCut)
    {
        std::vector<InkSample> samples{ { { 0, 0 }, 0.2f }, { { 100, 0 }, 0.6f } };
        auto pieces = *EraseCircle(samples, Half, { 100, 0 }, 49); // cut at x = 50
        ASSERT_EQ(1u, pieces.size());
        EXPECT_NEAR(0.4f, pieces[0].back().pressure, 1e-4);
    }

    TEST(StrokeEraser, ASegmentInsideOnlyForItsWiderNeighbourKeepsItsStart)
    {
        std::vector<InkSample> samples{ { { -10, 0 }, 1 }, { { 7, 0 }, 0 }, { { 30, 0 }, 0 } };
        auto half = [](InkSample const& s) { return s.pressure > 0.5f ? 6.0 : 1.0; };
        auto pieces = *EraseCircle(samples, half, { 0, 0 }, 2);
        EXPECT_EQ((PointF{ 30, 0 }), pieces.back().back().position);
        EXPECT_NEAR(7, pieces.back().front().position.x, 1e-4) << "b->c (outside for its own width) is kept from b on";
    }

    // ---- UndoHistory

    struct FakeEdit : Edit
    {
        FakeEdit(int page, std::vector<std::string>* log, std::string name) : page(page), log(log), name(std::move(name)) {}
        std::set<int> Pages() const override { return { page }; }
        bool Empty() const override { return name.empty(); }
        void Undo() override { log->push_back("undo " + name); }
        void Redo() override { log->push_back("redo " + name); }
        int page;
        std::vector<std::string>* log;
        std::string name;
    };

    TEST(UndoHistory, OneHistoryInTimeOrderAcrossPages)
    {
        std::vector<std::string> log;
        UndoHistory history;
        history.Push(std::make_shared<FakeEdit>(0, &log, "a"));
        history.Push(std::make_shared<FakeEdit>(1, &log, "b"));
        history.Push(std::make_shared<FakeEdit>(0, &log, "")); // empty: not recorded
        EXPECT_EQ((std::set<int>{ 0, 1 }), history.Pages());
        EXPECT_TRUE(history.Undo());
        EXPECT_TRUE(history.Undo());
        EXPECT_FALSE(history.Undo());
        EXPECT_TRUE(history.Redo());
        EXPECT_EQ((std::vector<std::string>{ "undo b", "undo a", "redo a" }), log);
        history.Push(std::make_shared<FakeEdit>(2, &log, "c"));
        EXPECT_FALSE(history.CanRedo()) << "a new edit drops the redo";
        EXPECT_EQ((std::set<int>{ 0, 2 }), history.Pages());
    }

    TEST(UndoHistory, SavePointAndForgettingAPage)
    {
        std::vector<std::string> log;
        UndoHistory history;
        EXPECT_TRUE(history.AtSavePoint());
        history.Push(std::make_shared<FakeEdit>(0, &log, "a"));
        EXPECT_FALSE(history.AtSavePoint());
        history.MarkSaved();
        EXPECT_TRUE(history.AtSavePoint());
        history.Push(std::make_shared<FakeEdit>(1, &log, "b"));
        EXPECT_FALSE(history.AtSavePoint());
        history.Undo();
        EXPECT_TRUE(history.AtSavePoint()) << "undone back to the save point";
        history.Forget(0);
        EXPECT_FALSE(history.AtSavePoint()) << "the saved edit's page is gone";
        EXPECT_FALSE(history.CanUndo());
        EXPECT_TRUE(history.CanRedo());
        history.Clear();
        EXPECT_TRUE(history.AtSavePoint());
    }
}
