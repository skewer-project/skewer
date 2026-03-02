#include <exrio/deep_writer.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "../test_helpers.h"
#include "deep_compositor.h"

using namespace deep_compositor;

// ============================================================================
// deepMerge correctness tests
// ============================================================================

class CompositorIntegrationTest : public ::testing::Test {
  protected:
    // Build a 1x1 image with a single point sample
    DeepImage make1x1Point(float z, float r, float g, float b, float a) {
        return makeImage1x1(z, r, g, b, a);
    }

    // Build a 1x1 image with a single volumetric sample
    DeepImage make1x1Volume(float zFront, float zBack, float r, float g, float b, float a) {
        return makeVolumeImage1x1(zFront, zBack, r, g, b, a);
    }
};

TEST_F(CompositorIntegrationTest, EmptyInputReturnsEmptyImage) {
    std::vector<DeepImage> inputs;
    DeepImage result = deepMerge(inputs);
    EXPECT_EQ(result.width(), 0);
    EXPECT_EQ(result.height(), 0);
}

TEST_F(CompositorIntegrationTest, SingleImagePassesThroughCorrectly) {
    DeepImage img = make1x1Point(1.0f, 0.8f, 0.6f, 0.4f, 0.9f);
    std::vector<DeepImage> inputs = {img};
    DeepImage result = deepMerge(inputs);
    EXPECT_EQ(result.width(), 1);
    EXPECT_EQ(result.height(), 1);
    ASSERT_EQ(result.pixel(0, 0).sampleCount(), 1u);
    EXPECT_FLOAT_EQ(result.pixel(0, 0)[0].alpha, 0.9f);
}

TEST_F(CompositorIntegrationTest, MismatchedDimensionsThrowsRuntimeError) {
    std::vector<DeepImage> inputs;
    inputs.emplace_back(4, 4);
    inputs.emplace_back(8, 8);
    EXPECT_THROW(deepMerge(inputs), std::runtime_error);
}

TEST_F(CompositorIntegrationTest, TwoImagesWithDisjointDepthsMergeInDepthOrder) {
    // Image A has sample at z=5 (far), Image B has sample at z=2 (close)
    DeepImage imgA = make1x1Point(5.0f, 0.5f, 0.5f, 0.5f, 0.7f);
    DeepImage imgB = make1x1Point(2.0f, 0.5f, 0.5f, 0.5f, 0.7f);
    std::vector<DeepImage> inputs = {imgA, imgB};
    DeepImage result = deepMerge(inputs);
    const auto& px = result.pixel(0, 0);
    ASSERT_EQ(px.sampleCount(), 2u);
    // Should be sorted: z=2 first, z=5 second
    EXPECT_FLOAT_EQ(px[0].depth, 2.0f);
    EXPECT_FLOAT_EQ(px[1].depth, 5.0f);
}

TEST_F(CompositorIntegrationTest, OutputIsAlwaysSorted) {
    DeepImage imgA = make1x1Point(3.0f, 0.5f, 0.5f, 0.5f, 0.6f);
    DeepImage imgB = make1x1Point(1.0f, 0.5f, 0.5f, 0.5f, 0.6f);
    DeepImage imgC = make1x1Point(2.0f, 0.5f, 0.5f, 0.5f, 0.6f);
    std::vector<DeepImage> inputs = {imgA, imgB, imgC};
    DeepImage result = deepMerge(inputs);
    EXPECT_TRUE(result.isValid());
}

TEST_F(CompositorIntegrationTest, ResultIsIndependentOfInputOrder) {
    DeepImage imgA = make1x1Point(1.0f, 0.8f, 0.0f, 0.0f, 0.5f);
    DeepImage imgB = make1x1Point(2.0f, 0.0f, 0.0f, 0.8f, 0.5f);

    std::vector<DeepImage> inputsAB = {imgA, imgB};
    std::vector<DeepImage> inputsBA = {imgB, imgA};

    DeepImage resultAB = deepMerge(inputsAB);
    DeepImage resultBA = deepMerge(inputsBA);

    auto flatAB = flattenImage(resultAB);
    auto flatBA = flattenImage(resultBA);

    ASSERT_EQ(flatAB.size(), flatBA.size());
    for (size_t i = 0; i < flatAB.size(); ++i) {
        EXPECT_NEAR(flatAB[i], flatBA[i], 1e-5f);
    }
}

TEST_F(CompositorIntegrationTest, FlattenedOutputAlphaClampedToOne) {
    // Two fully opaque images at different depths
    DeepImage imgA = make1x1Point(1.0f, 1.0f, 0.0f, 0.0f, 1.0f);
    DeepImage imgB = make1x1Point(2.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    std::vector<DeepImage> inputs = {imgA, imgB};
    DeepImage result = deepMerge(inputs);
    auto flat = flattenImage(result);
    EXPECT_LE(flat[3], 1.0f);
}

TEST_F(CompositorIntegrationTest, EmptyPixelsInAllInputsProduceEmptyOutputPixel) {
    DeepImage imgA(2, 2);
    DeepImage imgB(2, 2);
    std::vector<DeepImage> inputs = {imgA, imgB};
    DeepImage result = deepMerge(inputs);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            EXPECT_TRUE(result.pixel(x, y).isEmpty());
        }
    }
}

// ============================================================================
// CompositorStats tests
// ============================================================================

TEST_F(CompositorIntegrationTest, StatsPopulatedCorrectly) {
    DeepImage imgA = make1x1Point(1.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    DeepImage imgB = make1x1Point(2.0f, 0.5f, 0.5f, 0.5f, 0.6f);
    std::vector<DeepImage> inputs = {imgA, imgB};

    CompositorStats stats;
    deepMerge(inputs, CompositorOptions{}, &stats);

    EXPECT_EQ(stats.inputImageCount, 2u);
    EXPECT_EQ(stats.totalInputSamples, 2u);
    EXPECT_GT(stats.mergeTimeMs, 0.0);
}

TEST_F(CompositorIntegrationTest, DepthRangeCorrectlyReported) {
    DeepImage imgA = make1x1Point(1.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    DeepImage imgB = make1x1Volume(3.0f, 7.0f, 0.5f, 0.5f, 0.5f, 0.6f);
    std::vector<DeepImage> inputs = {imgA, imgB};

    CompositorStats stats;
    deepMerge(inputs, CompositorOptions{}, &stats);

    EXPECT_FLOAT_EQ(stats.minDepth, 1.0f);
    EXPECT_FLOAT_EQ(stats.maxDepth, 7.0f);
}

TEST_F(CompositorIntegrationTest, NullStatsPointerDoesNotCrash) {
    DeepImage img = make1x1Point(1.0f, 0.5f, 0.5f, 0.5f, 0.8f);
    std::vector<DeepImage> inputs = {img};
    EXPECT_NO_THROW(deepMerge(inputs, CompositorOptions{}, nullptr));
}

// ============================================================================
// CompositorOptions tests
// ============================================================================

TEST_F(CompositorIntegrationTest, DisablingMergingPreservesAllSamples) {
    // Two images with samples at the exact same depth
    DeepImage imgA = make1x1Point(1.0f, 0.5f, 0.5f, 0.5f, 0.5f);
    DeepImage imgB = make1x1Point(1.0f, 0.5f, 0.5f, 0.5f, 0.5f);
    std::vector<DeepImage> inputs = {imgA, imgB};

    CompositorOptions noMerge;
    noMerge.enableMerging = false;
    DeepImage result = deepMerge(inputs, noMerge);
    // With merging disabled, coincident samples should not be blended
    EXPECT_EQ(result.pixel(0, 0).sampleCount(), 2u);
}

TEST_F(CompositorIntegrationTest, MergeThresholdAffectsCollapseRadius) {
    // Two samples close but outside default epsilon
    DeepImage imgA = make1x1Point(1.0f, 0.5f, 0.5f, 0.5f, 0.5f);
    DeepImage imgB = make1x1Point(1.0005f, 0.5f, 0.5f, 0.5f, 0.5f);
    std::vector<DeepImage> inputs = {imgA, imgB};

    // Tiny threshold — samples far apart relative to threshold -> not merged
    CompositorOptions tinyThresh;
    tinyThresh.mergeThreshold = 0.0001f;
    DeepImage resultTiny = deepMerge(inputs, tinyThresh);
    EXPECT_EQ(resultTiny.pixel(0, 0).sampleCount(), 2u);

    // Large threshold — samples within threshold -> merged
    CompositorOptions bigThresh;
    bigThresh.mergeThreshold = 0.01f;
    DeepImage resultBig = deepMerge(inputs, bigThresh);
    EXPECT_EQ(resultBig.pixel(0, 0).sampleCount(), 1u);
}

// ============================================================================
// Classic compositing scenario tests
// ============================================================================

TEST_F(CompositorIntegrationTest, FrontSphereOccludesBackSphereOnFlatten) {
    // Fully opaque front (red) blocks blue behind it
    DeepImage front = make1x1Point(1.0f, 0.9f, 0.0f, 0.0f, 1.0f);
    DeepImage back = make1x1Point(5.0f, 0.0f, 0.0f, 0.9f, 1.0f);
    std::vector<DeepImage> inputs = {front, back};
    DeepImage merged = deepMerge(inputs);
    auto flat = flattenImage(merged);
    // Red should be ~0.9, blue should be ~0
    EXPECT_NEAR(flat[0], 0.9f, 1e-5f);
    EXPECT_NEAR(flat[2], 0.0f, 1e-5f);
    EXPECT_NEAR(flat[3], 1.0f, 1e-5f);
}

TEST_F(CompositorIntegrationTest, SemiTransparentFrontRevealsSomeOfBack) {
    // Semi-transparent red front, opaque blue back
    // front premul: red=0.5, alpha=0.5 (true red=1.0)
    // back premul: blue=0.9, alpha=1.0
    DeepImage front = make1x1Point(1.0f, 0.5f, 0.0f, 0.0f, 0.5f);
    DeepImage back = make1x1Point(5.0f, 0.0f, 0.0f, 0.9f, 1.0f);
    std::vector<DeepImage> inputs = {front, back};
    DeepImage merged = deepMerge(inputs);
    auto flat = flattenImage(merged);
    // Some blue should come through
    EXPECT_GT(flat[2], 0.0f);
    // Red should be present from front
    EXPECT_GT(flat[0], 0.0f);
}

TEST_F(CompositorIntegrationTest, AllPixelsInOutputHaveCorrectDimensions) {
    int w = 16, h = 16;
    DeepImage imgA(w, h);
    DeepImage imgB(w, h);
    std::vector<DeepImage> inputs = {imgA, imgB};
    DeepImage result = deepMerge(inputs);
    EXPECT_EQ(result.width(), w);
    EXPECT_EQ(result.height(), h);
}

TEST_F(CompositorIntegrationTest, PointerVersionProducesSameResultAsValueVersion) {
    DeepImage imgA = make1x1Point(1.0f, 0.8f, 0.3f, 0.2f, 0.7f);
    DeepImage imgB = make1x1Point(2.0f, 0.3f, 0.7f, 0.5f, 0.5f);

    std::vector<DeepImage> valueInputs = {imgA, imgB};
    std::vector<const DeepImage*> ptrInputs = {&imgA, &imgB};

    DeepImage valueResult = deepMerge(valueInputs);
    DeepImage ptrResult = deepMerge(ptrInputs);

    auto flatValue = flattenImage(valueResult);
    auto flatPtr = flattenImage(ptrResult);

    ASSERT_EQ(flatValue.size(), flatPtr.size());
    for (size_t i = 0; i < flatValue.size(); ++i) {
        EXPECT_NEAR(flatValue[i], flatPtr[i], 1e-5f);
    }
}
