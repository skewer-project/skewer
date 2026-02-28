#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "../test_helpers.h"
#include "deep_image.h"

using namespace exrio;

class DeepPixelTest : public ::testing::Test {
  protected:
    DeepSample makeSample(float z, float r = 0.5f, float g = 0.5f, float b = 0.5f, float a = 0.8f) {
        return makePoint(z, r, g, b, a);
    }
    DeepSample makeVolSample(float zFront, float zBack, float r = 0.5f, float g = 0.5f,
                             float b = 0.5f, float a = 0.6f) {
        return makeVolume(zFront, zBack, r, g, b, a);
    }
};

TEST_F(DeepPixelTest, DefaultConstructorIsEmpty) {
    DeepPixel p;
    EXPECT_TRUE(p.isEmpty());
    EXPECT_EQ(p.sampleCount(), 0u);
}

TEST_F(DeepPixelTest, MinDepthOnEmptyPixelReturnsPositiveInfinity) {
    DeepPixel p;
    EXPECT_TRUE(std::isinf(p.minDepth()));
    EXPECT_GT(p.minDepth(), 0.0f);
}

TEST_F(DeepPixelTest, MaxDepthOnEmptyPixelReturnsNegativeInfinity) {
    DeepPixel p;
    EXPECT_TRUE(std::isinf(p.maxDepth()));
    EXPECT_LT(p.maxDepth(), 0.0f);
}

TEST_F(DeepPixelTest, AddSingleSampleIncrementsSampleCount) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    EXPECT_EQ(p.sampleCount(), 1u);
    EXPECT_FALSE(p.isEmpty());
}

TEST_F(DeepPixelTest, AddSamplesInOrderPreservesSortOrder) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(2.0f));
    p.addSample(makeSample(3.0f));
    EXPECT_TRUE(p.isValidSortOrder());
}

TEST_F(DeepPixelTest, AddSamplesInReverseOrderStillSorted) {
    DeepPixel p;
    p.addSample(makeSample(3.0f));
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(2.0f));
    EXPECT_TRUE(p.isValidSortOrder());
    EXPECT_FLOAT_EQ(p[0].depth, 1.0f);
    EXPECT_FLOAT_EQ(p[1].depth, 2.0f);
    EXPECT_FLOAT_EQ(p[2].depth, 3.0f);
}

TEST_F(DeepPixelTest, AddSamplesWithDuplicateDepthsAllRetained) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(1.0f));
    EXPECT_EQ(p.sampleCount(), 2u);
}

TEST_F(DeepPixelTest, AddSamplesBatchInsertsAllAndSorts) {
    DeepPixel p;
    std::vector<DeepSample> batch = {makeSample(3.0f), makeSample(1.0f), makeSample(2.0f)};
    p.addSamples(batch);
    EXPECT_EQ(p.sampleCount(), 3u);
    EXPECT_TRUE(p.isValidSortOrder());
    EXPECT_FLOAT_EQ(p[0].depth, 1.0f);
}

TEST_F(DeepPixelTest, MinDepthReturnsSmallestFrontDepth) {
    DeepPixel p;
    p.addSample(makeSample(5.0f));
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(3.0f));
    EXPECT_FLOAT_EQ(p.minDepth(), 1.0f);
}

TEST_F(DeepPixelTest, MaxDepthReturnsLargestDepthBack) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    p.addSample(makeVolSample(2.0f, 6.0f));  // depth_back = 6.0
    p.addSample(makeSample(3.0f));
    EXPECT_FLOAT_EQ(p.maxDepth(), 6.0f);
}

TEST_F(DeepPixelTest, SortByDepthProducesValidOrder) {
    DeepPixel p;
    // Directly manipulate samples to create out-of-order state
    p.samples().push_back(makeSample(3.0f));
    p.samples().push_back(makeSample(1.0f));
    EXPECT_FALSE(p.isValidSortOrder());
    p.sortByDepth();
    EXPECT_TRUE(p.isValidSortOrder());
}

TEST_F(DeepPixelTest, MergeSamplesReducesNearbyPointSamplesToOne) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(1.0005f));  // within epsilon=0.001
    EXPECT_EQ(p.sampleCount(), 2u);
    p.mergeSamplesWithinEpsilon(0.001f);
    EXPECT_EQ(p.sampleCount(), 1u);
}

TEST_F(DeepPixelTest, MergeSamplesAveragesRGBA) {
    DeepPixel p;
    // Two point samples within epsilon: one with r=0.0, one with r=1.0 → average r=0.5
    DeepSample s1 = makePoint(1.0f, 0.0f, 0.0f, 0.0f, 0.4f);
    DeepSample s2 = makePoint(1.0005f, 1.0f, 1.0f, 1.0f, 0.8f);
    p.addSample(s1);
    p.addSample(s2);
    p.mergeSamplesWithinEpsilon(0.001f);
    ASSERT_EQ(p.sampleCount(), 1u);
    EXPECT_NEAR(p[0].red, 0.5f, 1e-5f);
    EXPECT_NEAR(p[0].green, 0.5f, 1e-5f);
    EXPECT_NEAR(p[0].blue, 0.5f, 1e-5f);
    EXPECT_NEAR(p[0].alpha, 0.6f, 1e-5f);  // (0.4 + 0.8) / 2 = 0.6
}

TEST_F(DeepPixelTest, MergeSamplesDoesNotMergeFarApartSamples) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(2.0f));  // well outside epsilon
    p.mergeSamplesWithinEpsilon(0.001f);
    EXPECT_EQ(p.sampleCount(), 2u);
}

TEST_F(DeepPixelTest, MergeOnSingleSampleIsNoOp) {
    DeepPixel p;
    p.addSample(makeSample(1.0f, 0.3f, 0.4f, 0.5f, 0.7f));
    p.mergeSamplesWithinEpsilon(0.001f);
    ASSERT_EQ(p.sampleCount(), 1u);
    EXPECT_FLOAT_EQ(p[0].depth, 1.0f);
    EXPECT_FLOAT_EQ(p[0].alpha, 0.7f);
}

TEST_F(DeepPixelTest, MergeOnEmptyPixelIsNoOp) {
    DeepPixel p;
    EXPECT_NO_THROW(p.mergeSamplesWithinEpsilon(0.001f));
    EXPECT_TRUE(p.isEmpty());
}

TEST_F(DeepPixelTest, IsValidSortOrderReturnsTrueForSortedSamples) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(2.0f));
    p.addSample(makeSample(3.0f));
    EXPECT_TRUE(p.isValidSortOrder());
}

TEST_F(DeepPixelTest, IsValidSortOrderReturnsFalseForUnsortedSamples) {
    DeepPixel p;
    p.samples().push_back(makeSample(3.0f));
    p.samples().push_back(makeSample(1.0f));
    EXPECT_FALSE(p.isValidSortOrder());
}

TEST_F(DeepPixelTest, IsValidSortOrderReturnsTrueForEmptyPixel) {
    DeepPixel p;
    EXPECT_TRUE(p.isValidSortOrder());
}

TEST_F(DeepPixelTest, ClearRemovesAllSamples) {
    DeepPixel p;
    p.addSample(makeSample(1.0f));
    p.addSample(makeSample(2.0f));
    p.clear();
    EXPECT_TRUE(p.isEmpty());
    EXPECT_EQ(p.sampleCount(), 0u);
}
