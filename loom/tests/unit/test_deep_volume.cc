#include <gtest/gtest.h>

#include <cmath>

#include "../test_helpers.h"
#include "deep_volume.h"

using namespace deep_compositor;

static constexpr float kTol = 1e-5f;

// ============================================================================
// splitSample tests
// ============================================================================

class SplitSampleTest : public ::testing::Test {};

TEST_F(SplitSampleTest, BeerLambertInvariantHoldsForMidpointSplit) {
    DeepSample vol = makeVolume(1.0f, 3.0f, 0.8f, 0.8f, 0.8f, 0.8f);
    float split = 2.0f;
    auto [front, back] = splitSample(vol, split);
    float invariant = (1.0f - front.alpha) * (1.0f - back.alpha);
    EXPECT_NEAR(invariant, 1.0f - vol.alpha, kTol);
}

TEST_F(SplitSampleTest, BeerLambertInvariantHoldsForAsymmetricSplit) {
    DeepSample vol = makeVolume(0.0f, 1.0f, 0.6f, 0.6f, 0.6f, 0.6f);
    float split = 0.3f;  // 30% into the volume
    auto [front, back] = splitSample(vol, split);
    float invariant = (1.0f - front.alpha) * (1.0f - back.alpha);
    EXPECT_NEAR(invariant, 1.0f - vol.alpha, kTol);
}

TEST_F(SplitSampleTest, BeerLambertInvariantHoldsForNearlyOpaqueSample) {
    DeepSample vol = makeVolume(0.0f, 2.0f, 0.999f, 0.999f, 0.999f, 0.999f);
    float split = 1.0f;
    auto [front, back] = splitSample(vol, split);
    float invariant = (1.0f - front.alpha) * (1.0f - back.alpha);
    EXPECT_NEAR(invariant, 1.0f - vol.alpha, 1e-4f);
}

TEST_F(SplitSampleTest, BeerLambertInvariantHoldsForNearlyTransparentSample) {
    DeepSample vol = makeVolume(0.0f, 2.0f, 0.001f, 0.001f, 0.001f, 0.001f);
    float split = 1.0f;
    auto [front, back] = splitSample(vol, split);
    float invariant = (1.0f - front.alpha) * (1.0f - back.alpha);
    EXPECT_NEAR(invariant, 1.0f - vol.alpha, kTol);
}

TEST_F(SplitSampleTest, FrontFragmentSpansDepthToSplitPoint) {
    DeepSample vol = makeVolume(1.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    float split = 2.0f;
    auto [front, back] = splitSample(vol, split);
    EXPECT_FLOAT_EQ(front.depth, 1.0f);
    EXPECT_FLOAT_EQ(front.depth_back, 2.0f);
}

TEST_F(SplitSampleTest, BackFragmentSpansSplitPointToDepthBack) {
    DeepSample vol = makeVolume(1.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    float split = 2.0f;
    auto [front, back] = splitSample(vol, split);
    EXPECT_FLOAT_EQ(back.depth, 2.0f);
    EXPECT_FLOAT_EQ(back.depth_back, 3.0f);
}

TEST_F(SplitSampleTest, PremultipliedRGBScalesProportionallyWithAlpha) {
    DeepSample vol = makeVolume(0.0f, 2.0f, 0.8f, 0.6f, 0.4f, 0.8f);
    float split = 1.0f;  // midpoint
    auto [front, back] = splitSample(vol, split);
    // RGB should scale with alphaFront/alpha and alphaBack/alpha respectively
    float ratioFront = front.alpha / vol.alpha;
    float ratioBack = back.alpha / vol.alpha;
    EXPECT_NEAR(front.red, vol.red * ratioFront, kTol);
    EXPECT_NEAR(front.green, vol.green * ratioFront, kTol);
    EXPECT_NEAR(front.blue, vol.blue * ratioFront, kTol);
    EXPECT_NEAR(back.red, vol.red * ratioBack, kTol);
    EXPECT_NEAR(back.green, vol.green * ratioBack, kTol);
    EXPECT_NEAR(back.blue, vol.blue * ratioBack, kTol);
}

TEST_F(SplitSampleTest, SplitAtExactFrontBoundaryDoesNotSplit) {
    DeepSample vol = makeVolume(1.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    auto [first, second] = splitSample(vol, 1.0f);
    EXPECT_FLOAT_EQ(first.depth, vol.depth);
    EXPECT_FLOAT_EQ(first.depth_back, vol.depth_back);
    EXPECT_FLOAT_EQ(first.alpha, vol.alpha);
    // second should be the zero sample
    EXPECT_FLOAT_EQ(second.alpha, 0.0f);
    EXPECT_FLOAT_EQ(second.depth, 0.0f);
}

TEST_F(SplitSampleTest, SplitAtExactBackBoundaryDoesNotSplit) {
    DeepSample vol = makeVolume(1.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    auto [first, second] = splitSample(vol, 3.0f);
    EXPECT_FLOAT_EQ(first.depth, vol.depth);
    EXPECT_FLOAT_EQ(first.depth_back, vol.depth_back);
    EXPECT_FLOAT_EQ(first.alpha, vol.alpha);
    EXPECT_FLOAT_EQ(second.alpha, 0.0f);
}

TEST_F(SplitSampleTest, SplitOutsideRangeDoesNotSplit) {
    DeepSample vol = makeVolume(1.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    auto [first, second] = splitSample(vol, 0.0f);  // before range
    EXPECT_FLOAT_EQ(first.alpha, vol.alpha);
    EXPECT_FLOAT_EQ(second.alpha, 0.0f);
}

TEST_F(SplitSampleTest, SplitOfPointSampleDoesNotSplit) {
    DeepSample point = makePoint(2.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    auto [first, second] = splitSample(point, 2.0f);
    EXPECT_FLOAT_EQ(first.depth, 2.0f);
    EXPECT_FLOAT_EQ(first.depth_back, 2.0f);
    EXPECT_FLOAT_EQ(first.alpha, 0.8f);
    EXPECT_FLOAT_EQ(second.alpha, 0.0f);
}

TEST_F(SplitSampleTest, SplitOfFullyTransparentSampleProducesTwoZeroAlphaFragments) {
    DeepSample vol = makeVolume(0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    auto [front, back] = splitSample(vol, 1.0f);
    EXPECT_FLOAT_EQ(front.alpha, 0.0f);
    EXPECT_FLOAT_EQ(back.alpha, 0.0f);
    EXPECT_FLOAT_EQ(front.depth, 0.0f);
    EXPECT_FLOAT_EQ(front.depth_back, 1.0f);
    EXPECT_FLOAT_EQ(back.depth, 1.0f);
    EXPECT_FLOAT_EQ(back.depth_back, 2.0f);
}

TEST_F(SplitSampleTest, SplitOfFullyOpaqueSampleClampsAlphaCorrectly) {
    DeepSample vol = makeVolume(0.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    auto [front, back] = splitSample(vol, 1.0f);
    // Each fragment should have alpha in [0, 1]
    EXPECT_GE(front.alpha, 0.0f);
    EXPECT_LE(front.alpha, 1.0f);
    EXPECT_GE(back.alpha, 0.0f);
    EXPECT_LE(back.alpha, 1.0f);
    // Invariant still holds approximately
    float invariant = (1.0f - front.alpha) * (1.0f - back.alpha);
    EXPECT_NEAR(invariant, 1.0f - vol.alpha, 1e-4f);
}

// ============================================================================
// blendCoincidentSamples tests
// ============================================================================

class BlendCoincidentTest : public ::testing::Test {};

TEST_F(BlendCoincidentTest, AlphaFormula) {
    DeepSample a = makeVolume(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    DeepSample b = makeVolume(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    DeepSample result = blendCoincidentSamples(a, b);
    // alpha = 1 - (1-0.5)(1-0.5) = 1 - 0.25 = 0.75
    EXPECT_NEAR(result.alpha, 0.75f, kTol);
}

TEST_F(BlendCoincidentTest, AlphaFormulaWithOneFullyOpaque) {
    DeepSample a = makeVolume(0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    DeepSample b = makeVolume(0.0f, 1.0f, 0.3f, 0.3f, 0.3f, 0.7f);
    DeepSample result = blendCoincidentSamples(a, b);
    EXPECT_NEAR(result.alpha, 1.0f, kTol);
}

TEST_F(BlendCoincidentTest, AlphaFormulaWithBothTransparent) {
    DeepSample a = makeVolume(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    DeepSample b = makeVolume(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    DeepSample result = blendCoincidentSamples(a, b);
    EXPECT_NEAR(result.alpha, 0.0f, kTol);
}

TEST_F(BlendCoincidentTest, RGBBlendedProportionally) {
    DeepSample a = makeVolume(0.0f, 1.0f, 0.4f, 0.2f, 0.1f, 0.5f);
    DeepSample b = makeVolume(0.0f, 1.0f, 0.2f, 0.1f, 0.05f, 0.5f);
    DeepSample result = blendCoincidentSamples(a, b);
    // alphaCombined = 0.75, alphaSum = 1.0, scale = 0.75
    float scale = 0.75f;
    EXPECT_NEAR(result.red, (0.4f + 0.2f) * scale, kTol);
    EXPECT_NEAR(result.green, (0.2f + 0.1f) * scale, kTol);
    EXPECT_NEAR(result.blue, (0.1f + 0.05f) * scale, kTol);
}

TEST_F(BlendCoincidentTest, DepthRangePreservedFromFirstSample) {
    DeepSample a = makeVolume(1.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.6f);
    DeepSample b = makeVolume(1.0f, 3.0f, 0.3f, 0.3f, 0.3f, 0.4f);
    DeepSample result = blendCoincidentSamples(a, b);
    EXPECT_FLOAT_EQ(result.depth, a.depth);
    EXPECT_FLOAT_EQ(result.depth_back, a.depth_back);
}

TEST_F(BlendCoincidentTest, BothZeroAlphaProducesZeroRGB) {
    DeepSample a = makeVolume(0.0f, 1.0f, 0.5f, 0.3f, 0.2f, 0.0f);
    DeepSample b = makeVolume(0.0f, 1.0f, 0.4f, 0.2f, 0.1f, 0.0f);
    DeepSample result = blendCoincidentSamples(a, b);
    EXPECT_FLOAT_EQ(result.alpha, 0.0f);
    EXPECT_FLOAT_EQ(result.red, 0.0f);
    EXPECT_FLOAT_EQ(result.green, 0.0f);
    EXPECT_FLOAT_EQ(result.blue, 0.0f);
}

// ============================================================================
// mergePixelsVolumetric tests
// ============================================================================

class MergePixelsVolumetricTest : public ::testing::Test {
  protected:
    DeepPixel makePixelWith(std::initializer_list<DeepSample> samples) {
        DeepPixel p;
        for (const auto& s : samples) p.addSample(s);
        return p;
    }
};

TEST_F(MergePixelsVolumetricTest, EmptyInputProducesEmptyPixel) {
    DeepPixel p;
    std::vector<const DeepPixel*> pixels = {&p};
    DeepPixel result = mergePixelsVolumetric(pixels);
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(MergePixelsVolumetricTest, SinglePixelWithSinglePointSamplePassthrough) {
    DeepPixel p;
    p.addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    std::vector<const DeepPixel*> pixels = {&p};
    DeepPixel result = mergePixelsVolumetric(pixels);
    ASSERT_EQ(result.sampleCount(), 1u);
    EXPECT_FLOAT_EQ(result[0].depth, 1.0f);
    EXPECT_FLOAT_EQ(result[0].alpha, 0.8f);
}

TEST_F(MergePixelsVolumetricTest, TwoNonOverlappingVolumesProduceTwoFragments) {
    DeepPixel pA, pB;
    pA.addSample(makeVolume(1.0f, 2.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    pB.addSample(makeVolume(3.0f, 4.0f, 0.5f, 0.5f, 0.5f, 0.6f));
    std::vector<const DeepPixel*> pixels = {&pA, &pB};
    DeepPixel result = mergePixelsVolumetric(pixels);
    EXPECT_EQ(result.sampleCount(), 2u);
}

TEST_F(MergePixelsVolumetricTest, TwoCompletelyCoincidentVolumesBlendIntoOne) {
    DeepPixel pA, pB;
    pA.addSample(makeVolume(1.0f, 2.0f, 0.4f, 0.4f, 0.4f, 0.5f));
    pB.addSample(makeVolume(1.0f, 2.0f, 0.3f, 0.3f, 0.3f, 0.5f));
    std::vector<const DeepPixel*> pixels = {&pA, &pB};
    DeepPixel result = mergePixelsVolumetric(pixels);
    ASSERT_EQ(result.sampleCount(), 1u);
    // Blended alpha: 1-(1-0.5)*(1-0.5) = 0.75
    EXPECT_NEAR(result[0].alpha, 0.75f, kTol);
}

TEST_F(MergePixelsVolumetricTest, OverlappingVolumesSplitAndBlendCorrectly) {
    // A: [1,3], B: [2,4] -> 3 non-overlapping intervals: [1,2], [2,3], [3,4]
    DeepPixel pA, pB;
    pA.addSample(makeVolume(1.0f, 3.0f, 0.6f, 0.6f, 0.6f, 0.8f));
    pB.addSample(makeVolume(2.0f, 4.0f, 0.4f, 0.4f, 0.4f, 0.6f));
    std::vector<const DeepPixel*> pixels = {&pA, &pB};
    DeepPixel result = mergePixelsVolumetric(pixels);
    EXPECT_EQ(result.sampleCount(), 3u);
}

TEST_F(MergePixelsVolumetricTest, BeerLambertInvariantPreservedAfterSplitAndBlend) {
    // Split a single volume, put halves in separate pixels, merge, verify invariant
    DeepSample orig = makeVolume(0.0f, 2.0f, 0.8f, 0.8f, 0.8f, 0.8f);
    auto [front, back] = splitSample(orig, 1.0f);

    DeepPixel pFront, pBack;
    pFront.addSample(front);
    pBack.addSample(back);
    std::vector<const DeepPixel*> pixels = {&pFront, &pBack};
    DeepPixel result = mergePixelsVolumetric(pixels);

    // Two non-overlapping fragments [0,1] and [1,2]
    ASSERT_EQ(result.sampleCount(), 2u);
    float invariant = (1.0f - result[0].alpha) * (1.0f - result[1].alpha);
    EXPECT_NEAR(invariant, 1.0f - orig.alpha, 1e-4f);
}

TEST_F(MergePixelsVolumetricTest, PointSampleInsideVolumeProducesThreeFragments) {
    // Volume [1,4] and point at z=2 -> [1,2], point[2,2], [2,4]
    DeepPixel pVol, pPoint;
    pVol.addSample(makeVolume(1.0f, 4.0f, 0.6f, 0.6f, 0.6f, 0.8f));
    pPoint.addSample(makePoint(2.0f, 0.5f, 0.5f, 0.5f, 0.5f));
    std::vector<const DeepPixel*> pixels = {&pVol, &pPoint};
    DeepPixel result = mergePixelsVolumetric(pixels);
    EXPECT_EQ(result.sampleCount(), 3u);
}

TEST_F(MergePixelsVolumetricTest, OutputSamplesAreSortedByDepth) {
    DeepPixel pA, pB;
    pA.addSample(makeVolume(3.0f, 5.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    pB.addSample(makeVolume(1.0f, 4.0f, 0.5f, 0.5f, 0.5f, 0.6f));
    std::vector<const DeepPixel*> pixels = {&pA, &pB};
    DeepPixel result = mergePixelsVolumetric(pixels);
    EXPECT_TRUE(result.isValidSortOrder());
}

TEST_F(MergePixelsVolumetricTest, MergeThreeOverlappingVolumes) {
    // A:[1,3], B:[2,4], C:[2.5,5] -> 5 intervals: [1,2],[2,2.5],[2.5,3],[3,4],[4,5]
    DeepPixel pA, pB, pC;
    pA.addSample(makeVolume(1.0f, 3.0f, 0.6f, 0.6f, 0.6f, 0.8f));
    pB.addSample(makeVolume(2.0f, 4.0f, 0.4f, 0.4f, 0.4f, 0.6f));
    pC.addSample(makeVolume(2.5f, 5.0f, 0.3f, 0.3f, 0.3f, 0.5f));
    std::vector<const DeepPixel*> pixels = {&pA, &pB, &pC};
    DeepPixel result = mergePixelsVolumetric(pixels);
    EXPECT_EQ(result.sampleCount(), 5u);
    EXPECT_TRUE(result.isValidSortOrder());
}
