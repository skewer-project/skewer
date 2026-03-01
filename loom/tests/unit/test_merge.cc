#include <gtest/gtest.h>
#include <vector>
#include "deep_row.h"
#include "deep_merger.h" // Assuming this contains RawSample and merge functions

class DeepPixelMergeTest : public ::testing::Test {
protected:
    // Helper to create a RawSample
    RawSample makeSample(float r, float g, float b, float a, float z, float zBack = -1.0f) {
        float finalZBack = (zBack < 0) ? z : zBack;
        return {r, g, b, a, z, finalZBack};
    }

    // Helper to convert RawSamples to the float* format DeepRow expects
    std::vector<float> packSamples(const std::vector<RawSample>& samples) {
        std::vector<float> data;
        for (const auto& s : samples) {
            data.insert(data.end(), {s.r, s.g, s.b, s.a, s.z, s.z_back});
        }
        return data;
    }
};

// ============================================================================
// RawSample Utility Tests
// ============================================================================

TEST_F(DeepPixelMergeTest, VolumetricSplittingMathIsCorrect) {
    // Layout: R, G, B, A, Z, ZBack
    RawSample vol = makeSample(0.4f, 0.4f, 0.4f, 0.5f, 0.0f, 10.0f);
    
    // Split exactly in the middle (zSplit = 5.0)
    auto [front, back] = splitSample(vol, 5.0f);
    
    EXPECT_FLOAT_EQ(front.z, 0.0f);
    EXPECT_FLOAT_EQ(front.z_back, 5.0f);
    EXPECT_FLOAT_EQ(back.z, 5.0f);
    EXPECT_FLOAT_EQ(back.z_back, 10.0f);
    
    // Alpha for 50% thickness: 1 - sqrt(1 - 0.5) ≈ 0.29289
    EXPECT_NEAR(front.a, 0.29289f, 1e-4f);
    EXPECT_FLOAT_EQ(front.a, back.a);
}

// ============================================================================
// sortAndMergePixelsDirect Tests
// ============================================================================

TEST_F(DeepPixelMergeTest, DirectMergeSortsByDepth) {
    DeepRow row;
    row.allocate(1, 10); 

    auto sFar = packSamples({makeSample(0.1f, 0.1f, 0.1f, 1.0f, 10.0f)});
    auto sNear = packSamples({makeSample(0.5f, 0.5f, 0.5f, 1.0f, 2.0f)});

    std::vector<const float*> ptrs = {sFar.data(), sNear.data()};
    std::vector<unsigned int> counts = {1, 1};

    sortAndMergePixelsDirect(0, ptrs, counts, row);

    ASSERT_EQ(row.getSampleCount(0), 2u);
    // Index [4] is Z. Sample 0 should be the near sample (Z=2.0)
    EXPECT_FLOAT_EQ(row.getSampleData(0, 0)[4], 2.0f);
    EXPECT_FLOAT_EQ(row.getSampleData(0, 1)[4], 10.0f);
}

TEST_F(DeepPixelMergeTest, DirectMergeIsOrderIndependent) {
    DeepRow rowA, rowB;
    rowA.allocate(1, 2);
    rowB.allocate(1, 2);

    auto s1 = packSamples({makeSample(1.0f, 0.0f, 0.0f, 0.5f, 1.0f)});
    auto s2 = packSamples({makeSample(0.0f, 0.0f, 1.0f, 0.5f, 2.0f)});

    sortAndMergePixelsDirect(0, {s1.data(), s2.data()}, {1, 1}, rowA);
    sortAndMergePixelsDirect(0, {s2.data(), s1.data()}, {1, 1}, rowB);

    // Compare Z values at index [4]
    EXPECT_FLOAT_EQ(rowA.getSampleData(0, 0)[4], rowB.getSampleData(0, 0)[4]);
    EXPECT_FLOAT_EQ(rowA.getSampleData(0, 1)[4], rowB.getSampleData(0, 1)[4]);
}

// ============================================================================
// sortAndMergePixelsWithSplit Tests
// ============================================================================

TEST_F(DeepPixelMergeTest, VolumetricSplitHandlesOverlappingSamples) {
    DeepRow row;
    row.allocate(1, 10);

    // Sample A: Volume from 0 to 10. Layout: R, G, B, A, Z, ZBack
    auto volA = packSamples({makeSample(0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 10.0f)});
    // Sample B: Point at 5.0
    auto ptB = packSamples({makeSample(1.0f, 0.0f, 0.0f, 1.0f, 5.0f)});

    sortAndMergePixelsWithSplit(0, {volA.data(), ptB.data()}, {1, 1}, row);

    ASSERT_EQ(row.getSampleCount(0), 3u);
    
    // Sample 0: Front vol segment (ZBack is index 5)
    EXPECT_FLOAT_EQ(row.getSampleData(0, 0)[5], 5.0f); 
    // Sample 1: The Point (Z is index 4)
    EXPECT_FLOAT_EQ(row.getSampleData(0, 1)[4], 5.0f); 
    // Sample 2: Back vol segment (Z is index 4)
    EXPECT_FLOAT_EQ(row.getSampleData(0, 2)[4], 5.0f); 
}

// ============================================================================
// Not Implemented Tests
// ============================================================================

// TEST_F(DeepPixelMergeTest, BlendsCoincidentSamplesCorrectly) {
//     DeepRow row;
//     row.allocate(1, 10);

//     // Two identical depth intervals (Z=1.0 to 2.0)
//     auto s1 = packSamples({makeSample(1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 2.0f)});
//     auto s2 = packSamples({makeSample(0.0f, 0.0f, 1.0f, 0.5f, 1.0f, 2.0f)});

//     std::vector<const float*> ptrs = {s1.data(), s2.data()};
//     std::vector<unsigned int> counts = {1, 1};

//     // Both 'Direct' and 'WithSplit' should merge coincident intervals
//     sortAndMergePixelsDirect(0, ptrs, counts, row);

//     ASSERT_EQ(row.getSampleCount(0), 1u);
    
//     // Volumetric transmission blending for Alpha: 1 - (1-0.5)*(1-0.5) = 0.75
//     EXPECT_FLOAT_EQ(row.getSampleData(0, 0)[3], 0.75f);
// }

// TEST_F(DeepPixelMergeTest, HandlesEmptyInputSources) {
//     DeepRow row;
//     row.allocate(1, 10);

//     std::vector<const float*> ptrs = {nullptr};
//     std::vector<unsigned int> counts = {0};

//     EXPECT_NO_THROW(sortAndMergePixelsDirect(0, ptrs, counts, row));
//     EXPECT_E