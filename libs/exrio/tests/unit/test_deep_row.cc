#include <gtest/gtest.h>

#include "exrio/deep_row.h"

namespace exrio {

class DeepRowTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Setup code here if needed
    }

    void TearDown() override {
        // Teardown code here if needed
    }
};

// ============================================================================
// Allocation Tests
// ============================================================================

TEST_F(DeepRowTest, DefaultConstructorInitializesEmpty) {
    DeepRow row;
    EXPECT_EQ(row.width, 0);
    EXPECT_EQ(row.total_samples_in_row, 0);
    EXPECT_EQ(row.all_samples.get(), nullptr);
    EXPECT_TRUE(row.sample_counts.empty());
    EXPECT_TRUE(row.sample_offsets.empty());
}

TEST_F(DeepRowTest, AllocateWithMaxSamplesInitializesCorrectly) {
    DeepRow row;
    const size_t width = 10;
    const int max_samples = 100;

    row.Allocate(width, max_samples);

    EXPECT_EQ(row.width, static_cast<int>(width));
    EXPECT_EQ(row.total_samples_in_row, max_samples);
    EXPECT_EQ(row.current_capacity, max_samples * 6);
    EXPECT_EQ(row.sample_counts.size(), width);
    EXPECT_EQ(row.sample_offsets.size(), width);
    EXPECT_NE(row.all_samples.get(), nullptr);

    // All counts should be initialized to 0
    for (size_t i = 0; i < width; ++i) {
        EXPECT_EQ(row.sample_counts[i], 0u);
        EXPECT_EQ(row.sample_offsets[i], 0u);
    }
}

TEST_F(DeepRowTest, AllocateWithCountsCalculatesOffsetsCorrectly) {
    DeepRow row;
    const size_t width = 5;
    unsigned int counts[] = {1, 2, 3, 2, 1};  // Total = 9 samples

    row.Allocate(width, counts);

    EXPECT_EQ(row.width, static_cast<int>(width));
    EXPECT_EQ(row.total_samples_in_row, 9);
    EXPECT_EQ(row.current_capacity, 9 * 6);

    // Check sample counts
    for (size_t i = 0; i < width; ++i) {
        EXPECT_EQ(row.sample_counts[i], counts[i]);
    }

    // Check offsets (cumulative sum)
    EXPECT_EQ(row.sample_offsets[0], 0);   // First pixel starts at 0
    EXPECT_EQ(row.sample_offsets[1], 1);   // Second starts at 1
    EXPECT_EQ(row.sample_offsets[2], 3);   // Third starts at 3
    EXPECT_EQ(row.sample_offsets[3], 6);   // Fourth starts at 6
    EXPECT_EQ(row.sample_offsets[4], 8);   // Fifth starts at 8
}

// ============================================================================
// Data Access Tests
// ============================================================================

TEST_F(DeepRowTest, GetSampleCountReturnsCorrectValue) {
    DeepRow row;
    unsigned int counts[] = {1, 5, 3};
    row.Allocate(3, counts);

    EXPECT_EQ(row.GetSampleCount(0), 1u);
    EXPECT_EQ(row.GetSampleCount(1), 5u);
    EXPECT_EQ(row.GetSampleCount(2), 3u);
}

TEST_F(DeepRowTest, GetPixelDataReturnsCorrectPointer) {
    DeepRow row;
    unsigned int counts[] = {2, 3, 1};
    row.Allocate(3, counts);

    // Initialize sample data to known values for verification
    float* pixel0_data = row.GetPixelData(0);
    float* pixel1_data = row.GetPixelData(1);
    float* pixel2_data = row.GetPixelData(2);

    EXPECT_NE(pixel0_data, nullptr);
    EXPECT_NE(pixel1_data, nullptr);
    EXPECT_NE(pixel2_data, nullptr);

    // Verify spacing: pixel1 should be 2*6 floats after pixel0
    EXPECT_EQ(pixel1_data - pixel0_data, 2 * 6);
    // Verify spacing: pixel2 should be 3*6 floats after pixel1
    EXPECT_EQ(pixel2_data - pixel1_data, 3 * 6);
}

TEST_F(DeepRowTest, GetSampleDataReturnsCorrectPointer) {
    DeepRow row;
    unsigned int counts[] = {3, 2};
    row.Allocate(2, counts);

    float* sample_0_0 = row.GetSampleData(0, 0);
    float* sample_0_1 = row.GetSampleData(0, 1);
    float* sample_0_2 = row.GetSampleData(0, 2);
    float* sample_1_0 = row.GetSampleData(1, 0);
    float* sample_1_1 = row.GetSampleData(1, 1);

    EXPECT_NE(sample_0_0, nullptr);
    EXPECT_NE(sample_0_1, nullptr);
    EXPECT_NE(sample_0_2, nullptr);
    EXPECT_NE(sample_1_0, nullptr);
    EXPECT_NE(sample_1_1, nullptr);

    // Samples within a pixel should be 6 floats apart
    EXPECT_EQ(sample_0_1 - sample_0_0, 6);
    EXPECT_EQ(sample_0_2 - sample_0_1, 6);
    EXPECT_EQ(sample_1_1 - sample_1_0, 6);

    // Sample boundary between pixels
    EXPECT_EQ(sample_1_0 - sample_0_2, 6);
}

TEST_F(DeepRowTest, GetSampleDataReturnsNullptrOutOfBounds) {
    DeepRow row;
    unsigned int counts[] = {2, 1};
    row.Allocate(2, counts);

    // Valid access
    EXPECT_NE(row.GetSampleData(0, 0), nullptr);
    EXPECT_NE(row.GetSampleData(0, 1), nullptr);

    // Out of bounds
    EXPECT_EQ(row.GetSampleData(0, 2), nullptr);  // Only 2 samples in pixel 0
    EXPECT_EQ(row.GetSampleData(1, 1), nullptr);  // Only 1 sample in pixel 1
}

// ============================================================================
// Movement and Lifecycle Tests
// ============================================================================

TEST_F(DeepRowTest, MoveConstructorTransfersOwnership) {
    DeepRow row1;
    unsigned int counts[] = {1, 2, 3};
    row1.Allocate(3, counts);

    float* original_ptr = row1.all_samples.get();
    int original_width = row1.width;
    size_t original_total = row1.total_samples_in_row;

    DeepRow row2(std::move(row1));

    // row2 should own the data
    EXPECT_EQ(row2.all_samples.get(), original_ptr);
    EXPECT_EQ(row2.width, original_width);
    EXPECT_EQ(row2.total_samples_in_row, original_total);

    // row1 should be reset
    EXPECT_EQ(row1.width, 0);
    EXPECT_EQ(row1.total_samples_in_row, 0);
}

TEST_F(DeepRowTest, MoveAssignmentTransfersOwnership) {
    DeepRow row1, row2;
    unsigned int counts[] = {1, 2};
    row1.Allocate(2, counts);

    float* original_ptr = row1.all_samples.get();
    int original_width = row1.width;

    row2 = std::move(row1);

    // row2 should own the data
    EXPECT_EQ(row2.all_samples.get(), original_ptr);
    EXPECT_EQ(row2.width, original_width);

    // row1 should be reset
    EXPECT_EQ(row1.width, 0);
}

TEST_F(DeepRowTest, CopyConstructorIsDeleted) {
    DeepRow row1;
    row1.Allocate(2, 10);

    // This should not compile
    // DeepRow row2(row1);  // Compiler error: deleted function
    EXPECT_TRUE(std::is_copy_constructible_v<DeepRow> == false);
}

TEST_F(DeepRowTest, CopyAssignmentIsDeleted) {
    DeepRow row1, row2;
    row1.Allocate(2, 10);

    // This should not compile
    // row2 = row1;  // Compiler error: deleted function
    EXPECT_TRUE(std::is_copy_assignable_v<DeepRow> == false);
}

TEST_F(DeepRowTest, ClearDeallocatesAndResets) {
    DeepRow row;
    unsigned int counts[] = {1, 2, 3};
    row.Allocate(3, counts);

    EXPECT_NE(row.all_samples.get(), nullptr);
    EXPECT_EQ(row.width, 3);

    row.Clear();

    EXPECT_EQ(row.all_samples.get(), nullptr);
    EXPECT_EQ(row.width, 0);
    EXPECT_EQ(row.total_samples_in_row, 0);
    EXPECT_TRUE(row.sample_counts.empty());
    EXPECT_TRUE(row.sample_offsets.empty());
}

// ============================================================================
// FlattenRow Tests
// ============================================================================

TEST_F(DeepRowTest, FlattenRowWithSingleSample) {
    DeepRow row;
    unsigned int counts[] = {1};
    row.Allocate(1, counts);

    // Set a single sample: R=0.5, G=0.3, B=0.2, A=0.8, Z=1.0, ZBack=1.5
    float* sample = row.GetPixelData(0);
    sample[0] = 0.5f;   // R (will be used as-is)
    sample[1] = 0.3f;   // G
    sample[2] = 0.2f;   // B
    sample[3] = 0.8f;   // A
    sample[4] = 1.0f;   // Z (not used in flatten)
    sample[5] = 1.5f;   // ZBack (not used in flatten)

    std::vector<float> output;
    FlattenRow(row, output);

    EXPECT_EQ(output.size(), 4u);
    EXPECT_FLOAT_EQ(output[0], 0.5f);   // R
    EXPECT_FLOAT_EQ(output[1], 0.3f);   // G
    EXPECT_FLOAT_EQ(output[2], 0.2f);   // B
    EXPECT_FLOAT_EQ(output[3], 0.8f);   // A
}

TEST_F(DeepRowTest, FlattenRowWithMultipleSamples) {
    DeepRow row;
    unsigned int counts[] = {2};
    row.Allocate(1, counts);

    // First sample: R=0.2, G=0.2, B=0.2, A=0.5
    float* sample0 = row.GetPixelData(0);
    sample0[0] = 0.2f;
    sample0[1] = 0.2f;
    sample0[2] = 0.2f;
    sample0[3] = 0.5f;

    // Second sample: R=0.4, G=0.3, B=0.2, A=0.6
    float* sample1 = row.GetSampleData(0, 1);
    sample1[0] = 0.4f;
    sample1[1] = 0.3f;
    sample1[2] = 0.2f;
    sample1[3] = 0.6f;

    std::vector<float> output;
    FlattenRow(row, output);

    // Over operation: result = sample0 + sample1 * (1 - alphaAccum)
    // After sample 0: R=0.2, G=0.2, B=0.2, A=0.5
    // After sample 1: alphaAccum=0.5, weight=0.5
    //   R = 0.2 + 0.4*0.5 = 0.4
    //   G = 0.2 + 0.3*0.5 = 0.35
    //   B = 0.2 + 0.2*0.5 = 0.3
    //   A = 0.5 + 0.6*0.5 = 0.8

    EXPECT_FLOAT_EQ(output[0], 0.4f);    // R
    EXPECT_FLOAT_EQ(output[1], 0.35f);   // G
    EXPECT_FLOAT_EQ(output[2], 0.3f);    // B
    EXPECT_FLOAT_EQ(output[3], 0.8f);    // A
}

TEST_F(DeepRowTest, FlattenRowEarlyOutAtOpacity) {
    DeepRow row;
    unsigned int counts[] = {2};
    row.Allocate(1, counts);

    // First sample: opaque
    float* sample0 = row.GetPixelData(0);
    sample0[0] = 1.0f;
    sample0[1] = 1.0f;
    sample0[2] = 1.0f;
    sample0[3] = 0.999f;  // >= 0.999f triggers early exit

    // Second sample: should not be processed
    float* sample1 = row.GetSampleData(0, 1);
    sample1[0] = 0.0f;
    sample1[1] = 0.0f;
    sample1[2] = 0.0f;
    sample1[3] = 0.5f;

    std::vector<float> output;
    FlattenRow(row, output);

    // Only first sample should be used
    EXPECT_FLOAT_EQ(output[0], 1.0f);    // R
    EXPECT_FLOAT_EQ(output[1], 1.0f);    // G
    EXPECT_FLOAT_EQ(output[2], 1.0f);    // B
    EXPECT_FLOAT_EQ(output[3], 0.999f);  // A (second sample ignored)
}

}  // namespace exrio
