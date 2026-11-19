#include <gtest/gtest.h>

#include "inkpages/model/StrokeKeys.h"

namespace inkpages::tests
{
    void ExpectNear(Affine const& expected, Affine const& actual)
    {
        EXPECT_NEAR(expected.m11, actual.m11, 1e-4);
        EXPECT_NEAR(expected.m22, actual.m22, 1e-4);
        EXPECT_NEAR(expected.m12, actual.m12, 1e-4);
        EXPECT_NEAR(expected.m21, actual.m21, 1e-4);
        EXPECT_NEAR(expected.dx, actual.dx, 1e-3);
        EXPECT_NEAR(expected.dy, actual.dy, 1e-3);
    }

    TEST(StrokeKeys, AnUnchangedStrokeKeepsItsKeyAcrossCopies)
    {
        StrokeKeys keys;
        keys.Loaded(10, 77, {}, { 24, 890 });
        keys.Copied(10, 11); // undo put back a Clone()
        auto found = keys.Find(11, { 24, 890 });
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(77u, found->key);
        EXPECT_TRUE(found->transform.IsIdentity());
        EXPECT_FALSE(keys.Find(12, { 0, 0 }).has_value()) << "new ink has no key";
        keys.Loaded(13, 0, {}, { 0, 0 });
        EXPECT_FALSE(keys.Find(13, { 0, 0 }).has_value()) << "key 0 is new ink";
    }

    TEST(StrokeKeys, AMoveIsTheTransformInPageCoordinates)
    {
        StrokeKeys keys;
        keys.Loaded(1, 5, {}, { 24, 24 });
        keys.Moved(1, 2, Affine::Translation(10, -4));
        ExpectNear(Affine::Translation(10, -4), keys.Find(2, { 24, 24 })->transform);
        // Scaled by 2 around a document point: the same scale around that point in page coordinates.
        keys.Moved(2, 3, Affine::ScaleAround(2, 124, 124));
        Affine expected = Affine::Translation(10, -4).Then(Affine::ScaleAround(2, 100, 100));
        ExpectNear(expected, keys.Find(3, { 24, 24 })->transform);
    }

    TEST(StrokeKeys, OntoAnotherPageTheTransformLeadsIntoThatPage)
    {
        StrokeKeys keys;
        keys.Loaded(1, 9, {}, { 24, 24 });
        keys.Moved(1, 2, Affine::Translation(0, 866)); // dragged down by one page (842 + gap 24)
        auto found = keys.Find(2, { 24, 890 });          // now held by page 2
        ExpectNear({}, found->transform);                 // the same place on the next page
        keys.Forget(2);
        EXPECT_FALSE(keys.Find(2, { 24, 890 }).has_value());
    }

    TEST(StrokeKeys, ALoadedTransformIsKept)
    {
        StrokeKeys keys;
        keys.Loaded(1, 3, Affine::Translation(5, 6), { 24, 890 }); // stored after an earlier move, loaded again
        ExpectNear(Affine::Translation(5, 6), keys.Find(1, { 24, 890 })->transform);
    }
}
