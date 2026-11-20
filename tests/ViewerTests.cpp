// The viewer's navigation without the window: panning, scroll keys, the whole page in view, places of the view
// (content rectangles handed to DirectManipulation; content pixels = document DIPs * scale) and Back/Forward.
#include <gtest/gtest.h>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/ViewHistory.h"
#include "inkpages/model/ViewMath.h"

namespace inkpages::tests
{
    namespace
    {
        constexpr SizeF Viewport{ 1000, 800 }; // pixels
        constexpr float Scale = 1.5f;          // pixels per DIP

        PageLayout ThreePages() { return PageLayout({ { 600, 800 }, { 600, 800 }, { 400, 300 } }, 24, 24); }
    }

    TEST(Viewer, PanRectMovesTheViewByViewportPixels)
    {
        View view{ 2, -300, -900 };
        RectF now = PanRect(Viewport, view, 0, 0);
        EXPECT_FLOAT_EQ(150, now.left);
        EXPECT_FLOAT_EQ(450, now.top);
        RectF moved = PanRect(Viewport, view, 40, -100);
        EXPECT_FLOAT_EQ(now.left + 20, moved.left) << "40 viewport pixels at zoom 2 are 20 content pixels";
        EXPECT_FLOAT_EQ(now.top - 50, moved.top);
        EXPECT_FLOAT_EQ(Viewport.width / 2, moved.Width()) << "the zoom stays";
    }

    TEST(Viewer, ScrollKeysStepLinesPagesAndGoToTheEnds)
    {
        View view{ 1, 0, -1000 };
        float docHeight = 4000; // DIPs: 6000 content pixels
        RectF down = KeyScrollRect(ScrollKey::LineDown, Viewport, view, Scale, docHeight, 60, 80);
        EXPECT_FLOAT_EQ(1060, down.top);
        RectF pageDown = KeyScrollRect(ScrollKey::PageDown, Viewport, view, Scale, docHeight, 60, 80);
        EXPECT_FLOAT_EQ(1000 + 720, pageDown.top) << "a view height less the overlap";
        RectF pageUp = KeyScrollRect(ScrollKey::PageUp, Viewport, view, Scale, docHeight, 60, 80);
        EXPECT_FLOAT_EQ(1000 - 720, pageUp.top);
        RectF right = KeyScrollRect(ScrollKey::LineRight, Viewport, view, Scale, docHeight, 60, 80);
        EXPECT_FLOAT_EQ(60, right.left);
        EXPECT_FLOAT_EQ(0, KeyScrollRect(ScrollKey::Start, Viewport, view, Scale, docHeight, 60, 80).top);
        RectF end = KeyScrollRect(ScrollKey::End, Viewport, view, Scale, docHeight, 60, 80);
        EXPECT_FLOAT_EQ(6000, end.bottom);
        EXPECT_FLOAT_EQ(800, end.Height());
    }

    TEST(Viewer, FitPageShowsTheWholePageCentered)
    {
        RectF page{ 24, 24, 624, 824 }; // 600x800 DIPs
        RectF r = FitPageRect(page, Viewport, Scale, 12, 0.2f, 6);
        float zoom = Viewport.height / r.Height();
        EXPECT_NEAR(800 / ((800 + 24) * Scale), zoom, 1e-4) << "the height limits it";
        EXPECT_NEAR((24 + 624) / 2.f * Scale, (r.left + r.right) / 2, 1e-3);
        EXPECT_NEAR((24 + 824) / 2.f * Scale, (r.top + r.bottom) / 2, 1e-3);
        EXPECT_GE(r.top, -1e-3f + 0) << "the page's top margin is in view";
    }

    TEST(Viewer, PlacesSurviveAPageInsertedAbove)
    {
        PageLayout layout = ThreePages();
        RectF second = layout.Page(1);
        RectF visible{ second.left - 10, second.top + 100, second.left + 500, second.top + 700 };
        auto place = PlaceOf(layout, visible, 1.25f);
        ASSERT_TRUE(place);
        EXPECT_EQ(1, place->page);
        EXPECT_FLOAT_EQ(-10, place->point.x);
        EXPECT_FLOAT_EQ(100, place->point.y);

        RectF r = PlaceRect(layout, *place, Viewport, Scale, 0.2f, 6);
        EXPECT_FLOAT_EQ(visible.left * Scale, r.left);
        EXPECT_FLOAT_EQ(visible.top * Scale, r.top);
        EXPECT_FLOAT_EQ(Viewport.width / 1.25f, r.Width());

        PageLayout four({ { 600, 800 }, { 600, 800 }, { 600, 800 }, { 400, 300 } }, 24, 24); // one more above
        RectF moved = PlaceRect(four, { 2, place->point, place->zoom }, Viewport, Scale, 0.2f, 6);
        EXPECT_FLOAT_EQ((four.Page(2).top + 100) * Scale, moved.top);
        RectF clamped = PlaceRect(layout, { 9, {}, 50 }, Viewport, Scale, 0.2f, 6);
        EXPECT_FLOAT_EQ(layout.Page(2).top * Scale, clamped.top) << "the last page";
        EXPECT_FLOAT_EQ(Viewport.width / 6, clamped.Width()) << "the zoom clamped";
    }

    TEST(ViewHistory, BackAndForwardWalkTheJumps)
    {
        ViewHistory history;
        EXPECT_FALSE(history.Back({}));
        ViewPlace a{ 0, { 0, 0 }, 1 }, b{ 4, { 0, 10 }, 1 }, c{ 9, { 0, 20 }, 2 };
        history.Visit(a); // a -> b
        history.Visit(b); // b -> c
        auto back = history.Back(c);
        ASSERT_TRUE(back);
        EXPECT_EQ(4, back->page);
        back = history.Back(*back);
        ASSERT_TRUE(back);
        EXPECT_EQ(0, back->page);
        EXPECT_FALSE(history.CanGoBack());
        auto forward = history.Forward(a);
        ASSERT_TRUE(forward);
        EXPECT_EQ(4, forward->page);
        history.Visit(*forward); // a new jump drops the way ahead
        EXPECT_FALSE(history.CanGoForward());
    }

    TEST(ViewHistory, TheSamePlaceIsKeptOnceAndPlacesFollowPages)
    {
        ViewHistory history;
        history.Visit({ 2, { 5, 5 }, 1 });
        history.Visit({ 2, { 5.4f, 5 }, 1 });
        history.Visit({ 5, { 0, 0 }, 1 });
        history.Remap(PageMap::Remove(6, 5)); // the page of the last place goes
        auto back = history.Back({ 0, {}, 1 });
        ASSERT_TRUE(back);
        EXPECT_EQ(2, back->page);
        EXPECT_FALSE(history.CanGoBack()) << "two visits of the same place are one";
        history.Remap(PageMap::Insert(6, 0, 1)); // the forward place (page 0) moves down
        auto forward = history.Forward(*back);
        ASSERT_TRUE(forward);
        EXPECT_EQ(1, forward->page);
    }
}
