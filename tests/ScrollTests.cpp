// Bringing a point or an area of a page into view at the current zoom (ScrollTo, RevealRect): the content rectangles
// handed to DirectManipulation (content pixels = document DIPs * scale; KeepOnContent clamps them afterwards).
#include <gtest/gtest.h>

#include "inkpages/model/ViewMath.h"

namespace inkpages::tests
{
    constexpr SizeF Viewport{ 1000, 800 }; // pixels
    constexpr float Scale = 1.5f;          // pixels per DIP
    constexpr float Margin = 24;           // 16 DIPs in viewport pixels

    TEST(ScrollToPoint, ThePointComesToTheTopLeftWithTheMargin)
    {
        View view{ 2, -300, -900 };
        RectF r = ScrollToPointRect(Viewport, view, Scale, { 100, 400 }, Margin);
        EXPECT_FLOAT_EQ(100 * Scale - Margin / 2, r.left);
        EXPECT_FLOAT_EQ(400 * Scale - Margin / 2, r.top);
        EXPECT_FLOAT_EQ(Viewport.width / 2, r.Width()) << "the zoom stays";
        EXPECT_FLOAT_EQ(Viewport.height / 2, r.Height());
    }

    TEST(ScrollToPoint, KeepOnContentClampsItAtTheDocumentsEnd)
    {
        View view{ 1, 0, 0 };
        SizeF content{ 900, 3000 };
        RectF r = KeepOnContent(ScrollToPointRect(Viewport, view, Scale, { 10, 1990 }, Margin), content);
        EXPECT_FLOAT_EQ(3000, r.bottom) << "not beyond the content's bottom";
        EXPECT_FLOAT_EQ(800, r.Height());
    }

    TEST(Reveal, AnAreaInViewDoesNotScroll)
    {
        View view{ 1, 0, -300 }; // content pixels 0..1000 x 300..1100 shown
        EXPECT_FALSE(RevealRect(Viewport, view, 1, { 100, 400, 200, 450 }, Margin).has_value());
    }

    TEST(Reveal, AnAreaBelowScrollsJustEnough)
    {
        View view{ 1, 0, -300 };
        auto r = RevealRect(Viewport, view, 1, { 100, 1200, 200, 1250 }, Margin);
        ASSERT_TRUE(r.has_value());
        EXPECT_FLOAT_EQ(1250 + Margin, r->bottom) << "its bottom (with the margin) at the view's bottom";
        EXPECT_FLOAT_EQ(0, r->left) << "no sideways move";
    }

    TEST(Reveal, AnAreaAboveOrTooCloseToTheEdgeScrollsUp)
    {
        View view{ 1, 0, -300 };
        auto above = RevealRect(Viewport, view, 1, { 100, 100, 200, 150 }, Margin);
        ASSERT_TRUE(above.has_value());
        EXPECT_FLOAT_EQ(100 - Margin, above->top);
        auto edge = RevealRect(Viewport, view, 1, { 100, 305, 200, 350 }, Margin);
        ASSERT_TRUE(edge.has_value()) << "inside, but within the margin";
        EXPECT_FLOAT_EQ(305 - Margin, edge->top);
    }

    TEST(Reveal, AnAreaLargerThanTheViewShowsItsStartAndZoomCounts)
    {
        View view{ 2, 0, 0 }; // content pixels 0..500 x 0..400 shown
        auto big = RevealRect(Viewport, view, 1, { 10, 600, 20, 1600 }, Margin);
        ASSERT_TRUE(big.has_value());
        EXPECT_FLOAT_EQ(600 - Margin / 2, big->top) << "its top, margin in content pixels at zoom 2";
        EXPECT_FLOAT_EQ(400, big->Height());
    }
}
