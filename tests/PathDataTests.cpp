#include <gtest/gtest.h>

#include "inkpages/model/PathData.h"

namespace inkpages::tests
{
    using Kind = PathSegment::Kind;

    TEST(PathData, ReadsTheAppsPageIcon)
    {
        // Xournal Studio's "delete page" icon (AppToolbar.cs): lines, an arc badge, nonzero fill.
        auto path = ParsePath(L"F1 M5,9.3 V3.5 L5.6,2.1 H12 Z M5,10.5 A4.5,4.5 0 1 1 5,19.5 A4.5,4.5 0 1 1 5,10.5 Z "
                              L"M2.75,14.3 V15.7 H7.25 V14.3 Z");
        ASSERT_TRUE(path.has_value());
        EXPECT_TRUE(path->nonzero);
        auto const& s = path->segments;
        ASSERT_GE(s.size(), 12u);
        EXPECT_EQ(Kind::Move, s[0].kind);
        EXPECT_EQ((PointF{ 5, 3.5f }), s[1].to) << "V keeps x";
        EXPECT_EQ((PointF{ 12, 2.1f }), s[3].to) << "H keeps y";
        EXPECT_EQ(Kind::Close, s[4].kind);
        EXPECT_EQ(Kind::Arc, s[6].kind);
        EXPECT_TRUE(s[6].largeArc);
        EXPECT_TRUE(s[6].clockwise);
        EXPECT_EQ((SizeF{ 4.5f, 4.5f }), s[6].radius);
    }

    TEST(PathData, RelativeCommandsAndImplicitLines)
    {
        auto path = ParsePath(L"m1,1 2,0 v3 h-2 c0,1 1,2 2,2 z");
        ASSERT_TRUE(path.has_value());
        EXPECT_FALSE(path->nonzero) << "even-odd by default";
        auto const& s = path->segments;
        EXPECT_EQ((PointF{ 3, 1 }), s[1].to) << "pairs after m are relative lines";
        EXPECT_EQ((PointF{ 3, 4 }), s[2].to);
        EXPECT_EQ((PointF{ 1, 4 }), s[3].to);
        EXPECT_EQ((PointF{ 1, 5 }), s[4].control1);
        EXPECT_EQ((PointF{ 3, 6 }), s[4].to);
    }

    TEST(PathData, RejectsWhatIsNotPathData)
    {
        EXPECT_FALSE(ParsePath(L"3,4 L5,6").has_value());
        EXPECT_FALSE(ParsePath(L"M1").has_value());
        EXPECT_FALSE(ParsePath(L"M1,1 Q2,2 3,3").has_value()) << "unsupported command";
        EXPECT_TRUE(ParsePath(L"").has_value());
    }
}
