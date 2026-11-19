#include <gtest/gtest.h>

#include "inkpages/model/TapGesture.h"

namespace inkpages::tests
{
    TEST(TapGesture, AShortStillPressIsATapAtThePressPoint)
    {
        TapGesture g;
        g.Press(1, { 10, 20 }, 1000);
        g.Move(1, { 11, 20.5f });
        EXPECT_FALSE(g.Moved());
        auto tap = g.Release(1, { 11, 21 }, 1200);
        ASSERT_TRUE(tap.has_value());
        EXPECT_EQ((PointF{ 10, 20 }), *tap);
        EXPECT_FALSE(g.Active());
    }

    TEST(TapGesture, TooLongOrTooFarIsNoTap)
    {
        TapGesture g;
        g.Press(1, { 0, 0 }, 0);
        EXPECT_FALSE(g.Release(1, { 0, 0 }, 300).has_value()) << "300 ms is a hold";
        g.Press(1, { 0, 0 }, 0);
        g.Move(1, { 2, 1 });
        EXPECT_TRUE(g.Moved()) << "|dx| + |dy| = 3 leaves the tap distance";
        EXPECT_FALSE(g.Release(1, { 0, 0 }, 50).has_value()) << "moving back does not make it a tap";
        g.Press(1, { 0, 0 }, 0);
        EXPECT_FALSE(g.Release(1, { 5, 0 }, 50).has_value()) << "the release point counts too";
    }

    TEST(TapGesture, ASecondPointerMakesItAPinch)
    {
        TapGesture g;
        g.Press(1, { 0, 0 }, 0);
        g.Press(2, { 50, 0 }, 10);
        EXPECT_FALSE(g.Release(2, { 50, 0 }, 40).has_value());
        EXPECT_TRUE(g.Active());
        EXPECT_FALSE(g.Release(1, { 0, 0 }, 60).has_value());
        EXPECT_FALSE(g.Active());
        g.Press(3, { 0, 0 }, 100);
        EXPECT_TRUE(g.Release(3, { 0, 0 }, 150).has_value()) << "the next gesture starts fresh";
    }

    TEST(TapGesture, ACancelledGestureIsNoTap)
    {
        TapGesture g;
        g.Press(1, { 0, 0 }, 0);
        g.Cancel();
        EXPECT_FALSE(g.Release(1, { 0, 0 }, 50).has_value());
        EXPECT_FALSE(g.Release(1, { 0, 0 }, 60).has_value()) << "a release without press is nothing";
        g.Cancel(); // no gesture: harmless
        g.Press(1, { 0, 0 }, 100);
        EXPECT_TRUE(g.Release(1, { 0, 0 }, 120).has_value());
    }
}
