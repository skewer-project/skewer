#include <gtest/gtest.h>
#include "deep_image.h"
#include "../test_helpers.h"

using namespace deep_compositor;

TEST(DeepSampleTest, DefaultConstructorZerosAllFields) {
    DeepSample s;
    EXPECT_FLOAT_EQ(s.depth, 0.0f);
    EXPECT_FLOAT_EQ(s.depth_back, 0.0f);
    EXPECT_FLOAT_EQ(s.red, 0.0f);
    EXPECT_FLOAT_EQ(s.green, 0.0f);
    EXPECT_FLOAT_EQ(s.blue, 0.0f);
    EXPECT_FLOAT_EQ(s.alpha, 0.0f);
}

TEST(DeepSampleTest, PointConstructorSetsDepthBackEqualToDepth) {
    DeepSample s(2.5f, 0.1f, 0.2f, 0.3f, 0.8f);
    EXPECT_FLOAT_EQ(s.depth, 2.5f);
    EXPECT_FLOAT_EQ(s.depth_back, 2.5f);
    EXPECT_FLOAT_EQ(s.red, 0.1f);
    EXPECT_FLOAT_EQ(s.green, 0.2f);
    EXPECT_FLOAT_EQ(s.blue, 0.3f);
    EXPECT_FLOAT_EQ(s.alpha, 0.8f);
}

TEST(DeepSampleTest, VolumetricConstructorSetsDistinctFrontBack) {
    DeepSample s(1.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.6f);
    EXPECT_FLOAT_EQ(s.depth, 1.0f);
    EXPECT_FLOAT_EQ(s.depth_back, 3.0f);
    EXPECT_NE(s.depth, s.depth_back);
}

TEST(DeepSampleTest, IsVolumeReturnsFalseForPointSample) {
    DeepSample s(1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_FALSE(s.isVolume());
}

TEST(DeepSampleTest, IsVolumeReturnsTrueWhenDepthBackGreater) {
    DeepSample s(1.0f, 2.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_TRUE(s.isVolume());
}

TEST(DeepSampleTest, ThicknessIsDepthBackMinusDepth) {
    DeepSample s(1.5f, 4.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_FLOAT_EQ(s.thickness(), 4.0f - 1.5f);
}

TEST(DeepSampleTest, ThicknessIsZeroForPointSample) {
    DeepSample s(2.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_FLOAT_EQ(s.thickness(), 0.0f);
}

TEST(DeepSampleTest, LessOperatorOrdersByDepth) {
    DeepSample a(1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    DeepSample b(2.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(DeepSampleTest, LessOperatorEqualDepthBreaksTieByDepthBack) {
    DeepSample a(1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    DeepSample b(1.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(DeepSampleTest, LessOperatorReturnsFalseForIdenticalDepths) {
    DeepSample a(1.0f, 2.0f, 0.1f, 0.2f, 0.3f, 0.5f);
    DeepSample b(1.0f, 2.0f, 0.4f, 0.5f, 0.6f, 0.7f);
    EXPECT_FALSE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(DeepSampleTest, IsNearDepthTrueWithinDefaultEpsilon) {
    DeepSample a(1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    DeepSample b(1.0005f, 2.0005f, 0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_TRUE(a.isNearDepth(b));
}

TEST(DeepSampleTest, IsNearDepthFalseWhenFrontDiffExceedsEpsilon) {
    DeepSample a(1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    DeepSample b(1.002f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_FALSE(a.isNearDepth(b));
}

TEST(DeepSampleTest, IsNearDepthFalseWhenBackDiffExceedsEpsilon) {
    DeepSample a(1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    DeepSample b(1.0f, 2.002f, 0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_FALSE(a.isNearDepth(b));
}

TEST(DeepSampleTest, IsNearDepthUsesCustomEpsilon) {
    DeepSample a(1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    DeepSample b(1.05f, 2.05f, 0.0f, 0.0f, 0.0f, 0.5f);
    // With default epsilon (0.001), far apart
    EXPECT_FALSE(a.isNearDepth(b));
    // With large custom epsilon, near
    EXPECT_TRUE(a.isNearDepth(b, 0.1f));
}
