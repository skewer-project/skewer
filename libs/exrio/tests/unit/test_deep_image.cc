#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "../test_helpers.h"
#include "deep_image.h"

using namespace exrio;

class DeepImageTest : public ::testing::Test {
  protected:
    DeepSample makeSample(float z, float a = 0.5f) { return makePoint(z, 0.5f, 0.5f, 0.5f, a); }
};

TEST_F(DeepImageTest, DefaultConstructorProducesZeroDimensions) {
    DeepImage img;
    EXPECT_EQ(img.width(), 0);
    EXPECT_EQ(img.height(), 0);
}

TEST_F(DeepImageTest, ParameterizedConstructorSetsDimensions) {
    DeepImage img(4, 8);
    EXPECT_EQ(img.width(), 4);
    EXPECT_EQ(img.height(), 8);
}

TEST_F(DeepImageTest, ConstructorWithZeroDimensionsIsValid) {
    EXPECT_NO_THROW(DeepImage img(0, 0));
    DeepImage img(0, 0);
    EXPECT_EQ(img.width(), 0);
    EXPECT_EQ(img.height(), 0);
}

TEST_F(DeepImageTest, ConstructorWithNegativeDimensionThrows) {
    EXPECT_THROW(DeepImage img(-1, 4), std::invalid_argument);
    EXPECT_THROW(DeepImage img(4, -1), std::invalid_argument);
}

TEST_F(DeepImageTest, PixelAccessWithValidCoordinatesDoesNotThrow) {
    DeepImage img(4, 4);
    EXPECT_NO_THROW(img.pixel(0, 0));
    EXPECT_NO_THROW(img.pixel(3, 3));
}

TEST_F(DeepImageTest, PixelAccessOutOfRangeThrowsOutOfRange) {
    DeepImage img(4, 4);
    EXPECT_THROW(img.pixel(4, 0), std::out_of_range);
    EXPECT_THROW(img.pixel(0, 4), std::out_of_range);
    EXPECT_THROW(img.pixel(-1, 0), std::out_of_range);
}

TEST_F(DeepImageTest, OperatorParenthesesEquivalentToPixelMethod) {
    DeepImage img(3, 3);
    img.pixel(1, 2).addSample(makeSample(5.0f));
    EXPECT_EQ(&img(1, 2), &img.pixel(1, 2));
    EXPECT_EQ(img(1, 2).sampleCount(), 1u);
}

TEST_F(DeepImageTest, TotalSampleCountStartsAtZero) {
    DeepImage img(4, 4);
    EXPECT_EQ(img.totalSampleCount(), 0u);
}

TEST_F(DeepImageTest, TotalSampleCountReflectsAddedSamples) {
    DeepImage img(2, 2);
    img.pixel(0, 0).addSample(makeSample(1.0f));
    img.pixel(0, 0).addSample(makeSample(2.0f));
    img.pixel(1, 1).addSample(makeSample(3.0f));
    EXPECT_EQ(img.totalSampleCount(), 3u);
}

TEST_F(DeepImageTest, NonEmptyPixelCountCountsOnlyPopulated) {
    DeepImage img(3, 3);
    img.pixel(0, 0).addSample(makeSample(1.0f));
    img.pixel(1, 1).addSample(makeSample(2.0f));
    EXPECT_EQ(img.nonEmptyPixelCount(), 2u);
}

TEST_F(DeepImageTest, DepthRangeOnEmptyImageGivesInfinities) {
    DeepImage img(4, 4);
    float minD, maxD;
    img.depthRange(minD, maxD);
    EXPECT_TRUE(std::isinf(minD));
    EXPECT_GT(minD, 0.0f);
    EXPECT_TRUE(std::isinf(maxD));
    EXPECT_LT(maxD, 0.0f);
}

TEST_F(DeepImageTest, DepthRangeSpansAllNonEmptyPixels) {
    DeepImage img(3, 3);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.5f));
    img.pixel(2, 2).addSample(makeVolume(5.0f, 10.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    float minD, maxD;
    img.depthRange(minD, maxD);
    EXPECT_FLOAT_EQ(minD, 1.0f);
    EXPECT_FLOAT_EQ(maxD, 10.0f);  // depth_back of volumetric sample
}

TEST_F(DeepImageTest, AverageSamplesPerPixelWithNoSamplesIsZero) {
    DeepImage img(4, 4);
    EXPECT_FLOAT_EQ(img.averageSamplesPerPixel(), 0.0f);
}

TEST_F(DeepImageTest, AverageSamplesPerPixelIsCorrect) {
    DeepImage img(2, 2);  // 4 pixels total
    img.pixel(0, 0).addSample(makeSample(1.0f));
    img.pixel(0, 0).addSample(makeSample(2.0f));
    img.pixel(1, 0).addSample(makeSample(3.0f));
    img.pixel(0, 1).addSample(makeSample(4.0f));
    // 4 samples / 4 pixels = 1.0
    EXPECT_FLOAT_EQ(img.averageSamplesPerPixel(), 1.0f);
}

TEST_F(DeepImageTest, SortAllPixelsMakesIsValidTrue) {
    DeepImage img(2, 2);
    // Manually insert out-of-order samples
    img.pixel(0, 0).samples().push_back(makeSample(3.0f));
    img.pixel(0, 0).samples().push_back(makeSample(1.0f));
    img.pixel(1, 1).samples().push_back(makeSample(5.0f));
    img.pixel(1, 1).samples().push_back(makeSample(2.0f));
    EXPECT_FALSE(img.isValid());
    img.sortAllPixels();
    EXPECT_TRUE(img.isValid());
}

TEST_F(DeepImageTest, IsValidReturnsTrueForFreshImage) {
    DeepImage img(4, 4);
    img.pixel(0, 0).addSample(makeSample(1.0f));
    img.pixel(0, 0).addSample(makeSample(2.0f));
    EXPECT_TRUE(img.isValid());
}

TEST_F(DeepImageTest, ResizeNewDimensionsAreReflected) {
    DeepImage img(4, 4);
    img.resize(8, 16);
    EXPECT_EQ(img.width(), 8);
    EXPECT_EQ(img.height(), 16);
}

TEST_F(DeepImageTest, ResizeClearsAllExistingData) {
    DeepImage img(2, 2);
    img.pixel(0, 0).addSample(makeSample(1.0f));
    img.resize(2, 2);
    EXPECT_EQ(img.totalSampleCount(), 0u);
}

TEST_F(DeepImageTest, ClearRemovesSamplesButPreservesDimensions) {
    DeepImage img(4, 4);
    img.pixel(0, 0).addSample(makeSample(1.0f));
    img.pixel(1, 2).addSample(makeSample(2.0f));
    img.clear();
    EXPECT_EQ(img.totalSampleCount(), 0u);
    EXPECT_EQ(img.width(), 4);
    EXPECT_EQ(img.height(), 4);
}

TEST_F(DeepImageTest, EstimatedMemoryUsageIsGreaterThanZero) {
    DeepImage img(4, 4);
    img.pixel(0, 0).addSample(makeSample(1.0f));
    EXPECT_GT(img.estimatedMemoryUsage(), 0u);
}

TEST_F(DeepImageTest, MemoryUsageIncreasesWithMoreSamples) {
    DeepImage img(4, 4);
    size_t before = img.estimatedMemoryUsage();
    for (int i = 0; i < 100; ++i) {
        img.pixel(0, 0).addSample(makeSample(static_cast<float>(i)));
    }
    size_t after = img.estimatedMemoryUsage();
    EXPECT_GT(after, before);
}
