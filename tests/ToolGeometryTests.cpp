#include <gtest/gtest.h>

#include <algorithm>

#include "inkpages/model/Geometry.h"
#include "inkpages/model/UndoHistory.h"

namespace inkpages::tests
{
    TEST(Affine, ComposesInvertsAndBoundsRectangles)
    {
        Affine move = Affine::Translation(10, 5);
        Affine scale = Affine::ScaleAround(2, 100, 100);
        PointF p = move.Then(scale).Apply(PointF{ 0, 0 }); // first move, then scale around (100,100)
        EXPECT_FLOAT_EQ(-80, p.x);
        EXPECT_FLOAT_EQ(-90, p.y);
        auto inverse = move.Then(scale).Inverse();
        ASSERT_TRUE(inverse.has_value());
        PointF back = inverse->Apply(p);
        EXPECT_NEAR(0, back.x, 1e-4);
        EXPECT_NEAR(0, back.y, 1e-4);
        EXPECT_EQ((RectF{ 100, 100, 120, 140 }), scale.Bounds(RectF{ 100, 100, 110, 120 }));
        EXPECT_TRUE(Affine{}.IsIdentity());
    }

    // ---- Tap (ported from TapTests)
    TEST(Tap, OnlyAShortStillPressIsATap)
    {
        EXPECT_TRUE(Tap::IsTap(0, 120, 1));
        EXPECT_TRUE(Tap::IsTap(2.5f, 250, 1));
        EXPECT_FALSE(Tap::IsTap(0.5f, 600, 1)) << "a slow tiny drag improves a sketch: it moves";
        EXPECT_FALSE(Tap::IsTap(2.9f, 300, 1));
        EXPECT_FALSE(Tap::IsTap(3, 50, 1));
        EXPECT_FALSE(Tap::IsTap(40, 100, 1));
        EXPECT_TRUE(Tap::IsTap(5, 100, 0.5f)) << "zoomed out: 5 page units are 2.5 DIPs on the screen";
        EXPECT_FALSE(Tap::IsTap(2, 100, 2)) << "zoomed in: 2 page units are 4 DIPs";
    }

    TEST(SelectionFrame, HandlesMoveAndProportionalScale)
    {
        SelectionFrame frame{ { 100, 100, 200, 150 }, 4, 8 }; // frame 96,96 - 204,154
        EXPECT_EQ(Handle::TopLeft, frame.At({ 96, 96 }));
        EXPECT_EQ(Handle::BottomRight, frame.At({ 205, 155 }));
        EXPECT_EQ(Handle::Move, frame.At({ 150, 120 }));
        EXPECT_EQ(Handle::None, frame.At({ 300, 300 }));
        Affine move = frame.Drag(Handle::Move, { 150, 120 }, { 150.4f, 121 });
        EXPECT_NEAR(0.4f, move.dx, 1e-4) << "a move is exact, however small";
        // Dragging the bottom-right corner 108 units right doubles the frame (108 wide) around the top-left corner.
        Affine scale = frame.Drag(Handle::BottomRight, { 204, 154 }, { 312, 160 });
        EXPECT_FLOAT_EQ(2, scale.m11);
        EXPECT_FLOAT_EQ(2, scale.m22) << "proportional";
        PointF anchor = scale.Apply(PointF{ 96, 96 });
        EXPECT_FLOAT_EQ(96, anchor.x);
        EXPECT_FLOAT_EQ(96, anchor.y);
        Affine tiny = frame.Drag(Handle::BottomRight, { 204, 154 }, { -500, -500 });
        EXPECT_GT(tiny.m11, 0) << "never mirrored or collapsed";
    }

    TEST(SelectionFrame, ActionsAreReachableAndRotationKeepsTheCenter)
    {
        SelectionFrame frame{ { 100, 100, 200, 150 }, 4, 8 };
        for (Handle h : { Handle::Rotate, Handle::Delete, Handle::Duplicate })
        {
            RectF r = frame.HandleRect(h);
            EXPECT_EQ(h, frame.At({ (r.left + r.right) / 2, (r.top + r.bottom) / 2 }));
        }
        Affine rotation = frame.Drag(Handle::Rotate, { 220, 125 }, { 150, 195 });
        PointF pivot = rotation.Apply(PointF{ 150, 125 });
        EXPECT_NEAR(150, pivot.x, 1e-4);
        EXPECT_NEAR(125, pivot.y, 1e-4);
        PointF edge = rotation.Apply(PointF{ 200, 125 });
        EXPECT_NEAR(150, edge.x, 1e-4);
        EXPECT_NEAR(175, edge.y, 1e-4);
    }

    TEST(ClipToRect, CutsAtTheEdgeAndDropsWhatIsOutside)
    {
        std::vector<InkSample> line{ { { -10, 5 }, 0.2f }, { { 30, 5 }, 0.6f } };
        auto pieces = ClipToRect(line, { 0, 0, 20, 10 });
        ASSERT_EQ(1u, pieces.size());
        EXPECT_FLOAT_EQ(0, pieces[0].front().position.x);
        EXPECT_FLOAT_EQ(20, pieces[0].back().position.x);
        EXPECT_NEAR(0.3f, pieces[0].front().pressure, 1e-5);
        // Out, in, out, in: two pieces.
        std::vector<InkSample> zigzag{ { { 5, -5 }, 0.5f }, { { 5, 5 }, 0.5f }, { { 5, 15 }, 0.5f }, { { 15, 15 }, 0.5f }, { { 15, 5 }, 0.5f } };
        EXPECT_EQ(2u, ClipToRect(zigzag, { 0, 0, 20, 10 }).size());
        EXPECT_TRUE(ClipToRect(line, { 100, 100, 120, 120 }).empty());
    }

    TEST(InPolygon, EvenOdd)
    {
        std::vector<PointF> square{ { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
        EXPECT_TRUE(InPolygon(square, { 5, 5 }));
        EXPECT_FALSE(InPolygon(square, { 15, 5 }));
    }

    // ---- ShapeGeometry (ported from ShapeGeometryTests)
    TEST(Shapes, RectangleIsClosedThroughAllCorners)
    {
        auto outline = ShapeOutline(ShapeKind::Rectangle, { 10, 20 }, { 50, 40 }, 1.41f);
        EXPECT_EQ((std::vector<PointF>{ { 10, 20 }, { 50, 20 }, { 50, 40 }, { 10, 40 }, { 10, 20 } }), outline);
    }

    TEST(Shapes, ArrowHeadSitsAtTheEnd)
    {
        auto outline = ShapeOutline(ShapeKind::Arrow, { 0, 0 }, { 100, 0 }, 1.41f);
        ASSERT_EQ(5u, outline.size());
        EXPECT_EQ((PointF{ 100, 0 }), outline[1]);
        EXPECT_EQ((PointF{ 100, 0 }), outline[3]);
        EXPECT_LT(outline[2].x, 100);
        EXPECT_NEAR(-outline[2].y, outline[4].y, 1e-4) << "one side above, one below the shaft";
        EXPECT_NE(0, outline[2].y);
    }

    TEST(Shapes, EllipseFitsTheDraggedRectangle)
    {
        auto outline = ShapeOutline(ShapeKind::Ellipse, { 0, 0 }, { 40, 20 }, 1.41f);
        EXPECT_EQ(outline.front(), outline.back());
        auto [minX, maxX] = std::minmax_element(outline.begin(), outline.end(), [](auto a, auto b) { return a.x < b.x; });
        EXPECT_NEAR(0, minX->x, 1e-4);
        EXPECT_NEAR(40, maxX->x, 1e-4);
        auto axes = EllipseFromAxes({ 20, 10 }, { { 0, 10 }, { 20, 0 }, { 40, 10 }, { 20, 20 } });
        EXPECT_NEAR(40, std::max_element(axes.begin(), axes.end(), [](auto a, auto b) { return a.x < b.x; })->x, 1e-3);
    }

    TEST(CompositeEdit, UndoesInReverseAndSkipsEmptyParts)
    {
        struct Logged : Edit
        {
            Logged(std::vector<std::string>* log, std::string name, int page) : log(log), name(std::move(name)), page(page) {}
            std::set<int> Pages() const override { return { page }; }
            bool Empty() const override { return false; }
            void Undo() override { log->push_back("undo " + name); }
            void Redo() override { log->push_back("redo " + name); }
            void RemapPages(PageMap const& map) override { page = map(page); }
            std::vector<std::string>* log;
            std::string name;
            int page;
        };
        std::vector<std::string> log;
        CompositeEdit both;
        both.Add(std::make_shared<Logged>(&log, "strokes", 0));
        both.Add(std::make_shared<Logged>(&log, "pictures", 1));
        both.Add(nullptr);
        EXPECT_EQ((std::set<int>{ 0, 1 }), both.Pages());
        both.Undo();
        both.Redo();
        EXPECT_EQ((std::vector<std::string>{ "undo pictures", "undo strokes", "redo strokes", "redo pictures" }), log);
    }
}
