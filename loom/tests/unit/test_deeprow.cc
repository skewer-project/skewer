#include <gtest/gtest.h>
#include <vector>
#include "deep_row.h"

class DeepRowTest : public ::testing::Test {
protected:
    /**
     * Updated Helper for the new Layout:
     * [0] R, [1] G, [2] B, [3] A, [4] Z (Front), [5] ZBack
     */
    void setSample(float* data, float z, float r, float g, float b, float a, float zBack = -1.0f) {
        data[0] = r;
        data[1] = g;
        data[2] = b;
        data[3] = a;
        data[4] = z;
        data[5] = (zBack < 0) ? z : zBack; // Point samples have Z == ZBack
    }
};

// Tests basic allocation and sample count retrieval
TEST_F(DeepRowTest, AllocationSetsCorrectWidthAndCounts) {
    DeepRow row;
    unsigned int counts[3] = {1, 2, 5};
    row.allocate(3, counts);

    EXPECT_EQ(row.getSampleCount(0), 1u);
    EXPECT_EQ(row.getSampleCount(1), 2u);
    EXPECT_EQ(row.getSampleCount(2), 5u);
}

// Tests that clearing resets the row state
TEST_F(DeepRowTest, ClearResetsRow) {
    DeepRow row;
    row.allocate(10, 100);
    row.clear();
    // Assuming clear resets or handles out-of-bounds gracefully
    EXPECT_EQ(row.getSampleCount(0), 0u); 
}

// Adapted from: SingleImagePassesThroughCorrectly
TEST_F(DeepRowTest, SingleSampleFlatteningIsCorrect) {
    DeepRow row;
    unsigned int counts[1] = {1};
    row.allocate(1, counts);
    
    float* sample = row.getSampleData(0, 0);
    // Setting Z=1.0, R=0.8, G=0.6, B=0.4, A=0.9
    setSample(sample, 1.0f, 0.8f, 0.6f, 0.4f, 0.9f);

    std::vector<float> rgba(4);
    flattenRow(row, rgba);

    EXPECT_FLOAT_EQ(rgba[0], 0.8f); // R
    EXPECT_FLOAT_EQ(rgba[3], 0.9f); // A
}

// Adapted from: FlattenedOutputAlphaClampedToOne
TEST_F(DeepRowTest, FlatteningClampsAlphaCorrectly) {
    DeepRow row;
    unsigned int counts[1] = {2};
    row.allocate(1, counts);

    // Two fully opaque samples at different depths
    setSample(row.getSampleData(0, 0), 1.0f, 1.0f, 0.0f, 0.0f, 1.0f);
    setSample(row.getSampleData(0, 1), 2.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    std::vector<float> rgba(4);
    flattenRow(row, rgba);

    // Alpha should be 1.0, never exceeding it
    EXPECT_LE(rgba[3], 1.0f);
    EXPECT_NEAR(rgba[3], 1.0f, 1e-5f);
}

// Adapted from: FrontSphereOccludesBackSphereOnFlatten
TEST_F(DeepRowTest, FrontSampleOccludesBackSample) {
    DeepRow row;
    unsigned int counts[1] = {2};
    row.allocate(1, counts);

    // Front: Opaque Red at Z=1.0
    setSample(row.getSampleData(0, 0), 1.0f, 0.9f, 0.0f, 0.0f, 1.0f);
    // Back: Opaque Blue at Z=5.0
    setSample(row.getSampleData(0, 1), 5.0f, 0.0f, 0.0f, 0.9f, 1.0f);

    std::vector<float> rgba(4);
    flattenRow(row, rgba);

    // Result should be Red; Blue is occluded by the opaque front sample
    EXPECT_NEAR(rgba[0], 0.9f, 1e-5f);
    EXPECT_NEAR(rgba[2], 0.0f, 1e-5f);
}

// Adapted from: SemiTransparentFrontRevealsSomeOfBack
TEST_F(DeepRowTest, SemiTransparentFrontRevealsBack) {
    DeepRow row;
    unsigned int counts[1] = {2};
    row.allocate(1, counts);

    // Front: 50% Alpha Red at Z=1.0
    setSample(row.getSampleData(0, 0), 1.0f, 0.5f, 0.0f, 0.0f, 0.5f);
    // Back: Opaque Blue at Z=5.0
    setSample(row.getSampleData(0, 1), 5.0f, 0.0f, 0.0f, 0.9f, 1.0f);

    std::vector<float> rgba(4);
    flattenRow(row, rgba);

    // Both Red and Blue should contribute to the final pixel
    EXPECT_GT(rgba[0], 0.0f); // Some Red
    EXPECT_GT(rgba[2], 0.0f); // Some Blue
}

// ============================================================================
// Not Implemented Tests
// ============================================================================

// Tests that the allocate(width, maxSamples) version resets data correctly
TEST_F(DeepRowTest, MaxSamplesAllocationResetsState) {
    DeepRow row;
    // Allocate space for 5 pixels and 20 total samples
    row.allocate(5, 20);
    
    // Initially, sample counts per pixel should be 0 even if buffer is allocated
    for(int i = 0; i < 5; ++i) {
        EXPECT_EQ(row.getSampleCount(i), 0u);
    }
}

// Tests that getPixelData and getSampleData point to the same base address for sample 0
TEST_F(DeepRowTest, DataPointerConsistency) {
    DeepRow row;
    unsigned int counts[1] = {1};
    row.allocate(1, counts);
    
    EXPECT_EQ(row.getPixelData(0), row.getSampleData(0, 0));
}