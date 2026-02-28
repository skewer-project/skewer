#include <gtest/gtest.h>

#include <cmath>

#include "../test_helpers.h"
#include "deep_image.h"
#include "deep_writer.h"

using namespace exrio;

static constexpr float kTol = 1e-5f;

class FlattenTest : public ::testing::Test {};

TEST_F(FlattenTest, EmptyPixelProducesAllZeroRGBA) {
    DeepPixel p;
    auto rgba = flattenPixel(p);
    EXPECT_FLOAT_EQ(rgba[0], 0.0f);
    EXPECT_FLOAT_EQ(rgba[1], 0.0f);
    EXPECT_FLOAT_EQ(rgba[2], 0.0f);
    EXPECT_FLOAT_EQ(rgba[3], 0.0f);
}

TEST_F(FlattenTest, SingleOpaqueSamplePassesThroughDirectly) {
    DeepPixel p;
    // Premultiplied: red=0.5, alpha=1.0 → full coverage
    p.addSample(makePoint(1.0f, 0.5f, 0.3f, 0.2f, 1.0f));
    auto rgba = flattenPixel(p);
    EXPECT_NEAR(rgba[0], 0.5f, kTol);
    EXPECT_NEAR(rgba[1], 0.3f, kTol);
    EXPECT_NEAR(rgba[2], 0.2f, kTol);
    EXPECT_NEAR(rgba[3], 1.0f, kTol);
}

TEST_F(FlattenTest, SingleTransparentSampleCompositeCorrectly) {
    DeepPixel p;
    // Premul red=0.25 (true red=0.5, alpha=0.5)
    p.addSample(makePoint(1.0f, 0.25f, 0.0f, 0.0f, 0.5f));
    auto rgba = flattenPixel(p);
    // accumR = 0.25 * (1-0) = 0.25; accumA = 0.5 * (1-0) = 0.5
    EXPECT_NEAR(rgba[0], 0.25f, kTol);
    EXPECT_NEAR(rgba[3], 0.5f, kTol);
}

TEST_F(FlattenTest, TwoLayerFrontToBackOver) {
    DeepPixel p;
    // Front: premul_red=0.5, alpha=0.5 (closer)
    p.addSample(makePoint(1.0f, 0.5f, 0.0f, 0.0f, 0.5f));
    // Back: premul_red=0.5, alpha=0.5 (farther)
    p.addSample(makePoint(2.0f, 0.5f, 0.0f, 0.0f, 0.5f));
    auto rgba = flattenPixel(p);
    // After front: accumR=0.5, accumA=0.5
    // After back:  accumR=0.5+0.5*(1-0.5)=0.75, accumA=0.5+0.5*0.5=0.75
    EXPECT_NEAR(rgba[0], 0.75f, kTol);
    EXPECT_NEAR(rgba[3], 0.75f, kTol);
}

TEST_F(FlattenTest, FullyOpaqueFrontLayerBlocksAllBehind) {
    DeepPixel p;
    // Opaque red front layer
    p.addSample(makePoint(1.0f, 0.8f, 0.0f, 0.0f, 1.0f));
    // Blue back layer — should be completely hidden
    p.addSample(makePoint(2.0f, 0.0f, 0.0f, 0.9f, 0.9f));
    auto rgba = flattenPixel(p);
    EXPECT_NEAR(rgba[0], 0.8f, kTol);  // red from front
    EXPECT_NEAR(rgba[2], 0.0f, kTol);  // no blue gets through
    EXPECT_NEAR(rgba[3], 1.0f, kTol);
}

TEST_F(FlattenTest, EarlyOutAt99Point99PercentAlpha) {
    DeepPixel p;
    // First sample reaches exactly 0.9999 alpha
    p.addSample(makePoint(1.0f, 0.9f, 0.0f, 0.0f, 0.9999f));
    // Second sample — should NOT be applied
    p.addSample(makePoint(2.0f, 0.0f, 0.0f, 0.5f, 0.5f));
    auto rgba = flattenPixel(p);
    // After first sample: accumA = 0.9999 → set to 1.0, break
    EXPECT_NEAR(rgba[3], 1.0f, kTol);
    // Blue channel should be ~0 (second sample not applied)
    EXPECT_NEAR(rgba[2], 0.0f, kTol);
}

TEST_F(FlattenTest, ResultAlphaIsNeverGreaterThanOne) {
    DeepPixel p;
    p.addSample(makePoint(1.0f, 0.8f, 0.8f, 0.8f, 0.9f));
    p.addSample(makePoint(2.0f, 0.8f, 0.8f, 0.8f, 0.9f));
    p.addSample(makePoint(3.0f, 0.8f, 0.8f, 0.8f, 0.9f));
    auto rgba = flattenPixel(p);
    EXPECT_LE(rgba[3], 1.0f);
}

TEST_F(FlattenTest, OrderMattersForFrontToBackComposite) {
    // Adding samples in either order should produce same result since addSample sorts
    DeepPixel pAB, pBA;
    DeepSample front = makePoint(1.0f, 0.5f, 0.0f, 0.0f, 0.5f);
    DeepSample back = makePoint(2.0f, 0.0f, 0.5f, 0.0f, 0.5f);

    pAB.addSample(front);
    pAB.addSample(back);

    pBA.addSample(back);
    pBA.addSample(front);

    auto rgbaAB = flattenPixel(pAB);
    auto rgbaBA = flattenPixel(pBA);

    EXPECT_NEAR(rgbaAB[0], rgbaBA[0], kTol);
    EXPECT_NEAR(rgbaAB[1], rgbaBA[1], kTol);
    EXPECT_NEAR(rgbaAB[2], rgbaBA[2], kTol);
    EXPECT_NEAR(rgbaAB[3], rgbaBA[3], kTol);
}

TEST_F(FlattenTest, OutputBufferSizeIsWidthTimesHeightTimesFour) {
    DeepImage img(5, 7);
    auto buf = flattenImage(img);
    EXPECT_EQ(buf.size(), static_cast<size_t>(5 * 7 * 4));
}

TEST_F(FlattenTest, PixelAtXYMapsToCorrectBufferOffset) {
    int w = 4, h = 4;
    DeepImage img(w, h);
    // Place a unique red value at pixel (2, 3)
    img.pixel(2, 3).addSample(makePoint(1.0f, 0.75f, 0.0f, 0.0f, 1.0f));
    auto buf = flattenImage(img);
    size_t idx = (static_cast<size_t>(3) * w + 2) * 4;
    EXPECT_NEAR(buf[idx + 0], 0.75f, kTol);  // red
    EXPECT_NEAR(buf[idx + 3], 1.0f, kTol);   // alpha
}

TEST_F(FlattenTest, AllEmptyPixelsProduceZeroBuffer) {
    DeepImage img(3, 3);
    auto buf = flattenImage(img);
    for (float v : buf) {
        EXPECT_FLOAT_EQ(v, 0.0f);
    }
}
