#include <gtest/gtest.h>

#include <algorithm>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/Paper.h"
#include "inkpages/model/ViewMath.h"

namespace inkpages::tests
{
    // ---- Paper (ported from the WinUI version's PaperTests)

    TEST(Paper, GraphPaperHasLinesBothWaysIncludingTheEdges)
    {
        auto lines = Paper{ PaperKind::Graph, 24 }.Lines(48, 72);
        std::vector<float> vertical, horizontal;
        for (Line const& l : lines)
        {
            if (l.from.x == l.to.x) vertical.push_back(l.from.x);
            if (l.from.y == l.to.y) horizontal.push_back(l.from.y);
        }
        EXPECT_EQ((std::vector<float>{ 0, 24, 48 }), vertical);
        EXPECT_EQ((std::vector<float>{ 0, 24, 48, 72 }), horizontal);
    }

    TEST(Paper, LinedPaperHasHorizontalLinesOnly)
    {
        auto lines = Paper{ PaperKind::Lined, 30 }.Lines(100, 100);
        for (Line const& l : lines) EXPECT_EQ(l.from.y, l.to.y);
        EXPECT_EQ(4u, lines.size()); // 0, 30, 60, 90
    }

    TEST(Paper, PlainPaperAndOddSizesHaveNoLines)
    {
        EXPECT_TRUE(Paper{ PaperKind::Plain }.Lines(100, 100).empty());
        EXPECT_TRUE((Paper{ PaperKind::Graph, 0 }.Lines(100, 100).empty()));
        EXPECT_TRUE(Paper{ PaperKind::Graph }.Lines(0, 100).empty());
        EXPECT_EQ(Paper::DefaultLineColor, Paper{ PaperKind::Graph }.lineColor);
    }

    TEST(Paper, LinesStayOnTheSheetOverALongPage)
    {
        // Counted, not summed: the last line of an A4 page at 0.5 DIP spacing is not lost or doubled by float drift.
        auto lines = Paper{ PaperKind::Lined, 0.5f }.Lines(10, 842);
        EXPECT_EQ(1685u, lines.size());
        for (Line const& l : lines) EXPECT_LE(l.from.y, 842.0f);
    }

    // ---- PageLayout

    PageLayout ThreeA4() { return PageLayout({ { 595, 842 }, { 595, 842 }, { 595, 842 } }, 24, 24); }

    TEST(PageLayout, StacksPagesVerticallyWithGapAndMargin)
    {
        PageLayout layout = ThreeA4();
        EXPECT_EQ(3, layout.Count());
        EXPECT_EQ((RectF{ 24, 24, 619, 866 }), layout.Page(0));
        EXPECT_EQ((RectF{ 24, 890, 619, 1732 }), layout.Page(1));
        EXPECT_EQ((SizeF{ 643, 3 * 842 + 4 * 24 }), layout.DocumentSize());
    }

    TEST(PageLayout, NarrowerPagesAreCentered)
    {
        PageLayout layout({ { 600, 100 }, { 400, 100 } }, 10, 20);
        EXPECT_EQ(120, layout.Page(1).left);
        EXPECT_EQ(520, layout.Page(1).right);
        EXPECT_EQ(640, layout.DocumentSize().width);
    }

    TEST(PageLayout, PageAtAndNearestPage)
    {
        PageLayout layout = ThreeA4();
        EXPECT_EQ(1, layout.PageAt({ 100, 900 }));
        EXPECT_FALSE(layout.PageAt({ 100, 870 }).has_value()) << "the gap belongs to no page";
        EXPECT_EQ(0, layout.NearestPage({ 100, 870 }));
        EXPECT_EQ(1, layout.NearestPage({ 100, 886 }));
        EXPECT_EQ(2, layout.NearestPage({ 100, 99999 }));
        EXPECT_FALSE(PageLayout().NearestPage({ 0, 0 }).has_value());
    }

    TEST(PageLayout, PageInViewCoversMostOfTheView)
    {
        PageLayout layout = ThreeA4();
        EXPECT_EQ(0, layout.PageInView({ 0, 0, 643, 500 }));
        EXPECT_EQ(1, layout.PageInView({ 0, 700, 643, 1200 })) << "page 2 covers more than page 1";
        EXPECT_EQ((std::vector<int>{ 0, 1 }), layout.PagesIn({ 0, 700, 643, 1200 }));
    }

    // ---- ViewMath

    TEST(ViewMath, ZoomIsClampedAndFitWidthFills)
    {
        EXPECT_EQ(0.2f, ClampZoom(0.05f, 0.2f, 6));
        EXPECT_EQ(6.0f, ClampZoom(9, 0.2f, 6));
        EXPECT_EQ(1.5f, ClampZoom(1.5f, 0.2f, 6));
        EXPECT_FLOAT_EQ(2.0f, FitWidthZoom(643, 643 * 1.5f * 2, 1.5f));
    }

    TEST(ViewMath, DocumentAndViewportRoundTrip)
    {
        View view{ 2.5f, -300, -1200 };
        PointF doc{ 123.5f, 900.25f };
        PointF px = DocumentToViewport(doc, view, 1.5f);
        EXPECT_FLOAT_EQ(123.5f * 1.5f * 2.5f - 300, px.x);
        PointF back = ViewportToDocument(px, view, 1.5f);
        EXPECT_NEAR(doc.x, back.x, 1e-3);
        EXPECT_NEAR(doc.y, back.y, 1e-3);
        RectF visible = VisibleDocument({ 1500, 900 }, view, 1.5f);
        EXPECT_NEAR(300 / 3.75f, visible.left, 1e-3);
        EXPECT_NEAR(900 / 3.75f, visible.Height(), 1e-3);
    }

    TEST(ViewMath, ZoomRectKeepsTheAnchor)
    {
        View view{ 1, 50, -100 };
        SizeF viewport{ 1000, 800 };
        PointF anchor{ 400, 300 };
        RectF r = ZoomRect(viewport, view, 4, anchor);
        // After zooming to r the viewport shows r: content point under the anchor = left + anchor / zoom.
        float contentX = (anchor.x - view.dx) / view.zoom, contentY = (anchor.y - view.dy) / view.zoom;
        EXPECT_FLOAT_EQ(contentX, r.left + anchor.x / 4);
        EXPECT_FLOAT_EQ(contentY, r.top + anchor.y / 4);
        EXPECT_FLOAT_EQ(250, r.Width());
        RectF s = ScrollRect(viewport, view, 1.5f, 890);
        EXPECT_FLOAT_EQ(890 * 1.5f, s.top);
        EXPECT_FLOAT_EQ(-50, s.left);
    }

    TEST(ViewMath, ZoomRectsStayOnTheContent)
    {
        SizeF content{ 1000, 5000 };
        EXPECT_EQ((RectF{ 0, 0, 400, 300 }), KeepOnContent({ -50, -100, 350, 200 }, content)) << "moved inside";
        EXPECT_EQ((RectF{ 600, 4700, 1000, 5000 }), KeepOnContent({ 700, 4900, 1100, 5200 }, content));
        EXPECT_EQ((RectF{ -500, 0, 1500, 6000 }), KeepOnContent({ -100, -300, 1900, 5700 }, content))
            << "wider: centered; taller: from the top";
    }

    TEST(ViewMath, LivePagesComeNearAndGoFarWithHeldPagesKept)
    {
        std::vector<SizeF> sizes(40, SizeF{ 595, 842 });
        PageLayout layout(sizes, 24, 24);
        RectF visible{ 0, 0, 643, 500 }; // page 0 in view
        std::set<int> live = LivePages(layout, visible, {}, {});
        EXPECT_EQ((std::set<int>{ 0, 1 }), live) << "page 1 starts within one view height below";
        // Scroll to page 10: page 0 is far away and goes; page 10 (and its near neighbors) come.
        RectF later{ 0, layout.Page(10).top, 643, layout.Page(10).top + 500 };
        std::set<int> next = LivePages(layout, later, live, { 3 });
        EXPECT_TRUE(next.contains(10));
        EXPECT_FALSE(next.contains(0));
        EXPECT_TRUE(next.contains(3)) << "held";
        // A page between one and two view heights away stays once live (no flicker at the edge).
        RectF slightly{ 0, layout.Page(10).top + 700, 643, layout.Page(10).top + 1200 };
        std::set<int> kept = LivePages(layout, slightly, { 9, 10 }, {});
        EXPECT_TRUE(kept.contains(9));
    }
}
