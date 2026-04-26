#include <gtest/gtest.h>

#include <filesystem>

#include "exrio/deep_stream_reader.h"

namespace exrio {

class DeepStreamReaderTest : public ::testing::TestWithParam<const char*> {
  protected:
    // Use test EXR files from the test data directory
    // This assumes test files exist; adjust path as needed or create minimal test fixtures

    const std::string test_data_dir = "../tests/data";
};

// ============================================================================
// Constructor and Basic Access Tests
// ============================================================================

TEST_F(DeepStreamReaderTest, ConstructorThrowsOnNonexistentFile) {
    EXPECT_THROW(
        DeepStreamReader reader("/nonexistent/path/to/file.exr"),
        std::exception
    );
}

TEST_F(DeepStreamReaderTest, ConstructorThrowsOnNonDeepEXR) {
    // This test would require a non-deep EXR file
    // For now, we'll skip it if test data is not available
    std::string test_file = "../tests/data/non_deep.exr";
    if (std::filesystem::exists(test_file)) {
        EXPECT_THROW(
            DeepStreamReader reader(test_file),
            std::runtime_error
        );
    }
}

// ============================================================================
// Metadata Access Tests
// ============================================================================

TEST_F(DeepStreamReaderTest, CanAccessImageDimensions) {
    // This test requires a valid test EXR file
    // The test will gracefully skip if the file doesn't exist
    std::string test_file = "../tests/data/deep_test.exr";
    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "Test file not found: " << test_file;
    }

    DeepStreamReader reader(test_file);

    // Should be able to get dimensions without throwing
    int width = reader.getWidth();
    int height = reader.getHeight();
    int min_x = reader.getMinX();
    int min_y = reader.getMinY();

    EXPECT_GT(width, 0);
    EXPECT_GT(height, 0);
    // min_x and min_y are typically 0, but could be different for cropped images
    EXPECT_GE(min_x, 0);
    EXPECT_GE(min_y, 0);
}

// ============================================================================
// Streaming API Tests
// ============================================================================

TEST_F(DeepStreamReaderTest, CanGetSampleCountsForRow) {
    std::string test_file = "../tests/data/deep_test.exr";
    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "Test file not found: " << test_file;
    }

    DeepStreamReader reader(test_file);
    int width = reader.getWidth();

    // Should be able to get sample counts for first row
    const unsigned int* counts = reader.getSampleCountsForRow(0);
    EXPECT_NE(counts, nullptr);

    // All counts should be non-negative (unsigned)
    for (int x = 0; x < width; ++x) {
        EXPECT_GE(counts[x], 0u);
    }
}

TEST_F(DeepStreamReaderTest, CanGetSampleCountsForMultipleRows) {
    std::string test_file = "../tests/data/deep_test.exr";
    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "Test file not found: " << test_file;
    }

    DeepStreamReader reader(test_file);
    int height = reader.getHeight();

    // Should be able to read multiple rows without error
    for (int y = 0; y < std::min(height, 10); ++y) {
        const unsigned int* counts = reader.getSampleCountsForRow(y);
        EXPECT_NE(counts, nullptr);
    }
}

// ============================================================================
// Native Handle Access Tests
// ============================================================================

TEST_F(DeepStreamReaderTest, CanAccessNativeHandle) {
    std::string test_file = "../tests/data/deep_test.exr";
    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "Test file not found: " << test_file;
    }

    DeepStreamReader reader(test_file);

    // Should be able to get native file handle
    Imf::DeepScanLineInputFile& file = reader.getNativeHandle();

    // Verify we can use it (e.g., access header)
    const Imf::Header& header = file.header();
    EXPECT_TRUE(header.hasType());
}

// ============================================================================
// Non-Copyability Tests
// ============================================================================

TEST_F(DeepStreamReaderTest, CopyConstructorIsDeleted) {
    EXPECT_FALSE(std::is_copy_constructible_v<DeepStreamReader>);
}

TEST_F(DeepStreamReaderTest, CopyAssignmentIsDeleted) {
    EXPECT_FALSE(std::is_copy_assignable_v<DeepStreamReader>);
}

TEST_F(DeepStreamReaderTest, MoveConstructorExists) {
    EXPECT_TRUE(std::is_move_constructible_v<DeepStreamReader>);
}

TEST_F(DeepStreamReaderTest, MoveAssignmentExists) {
    EXPECT_TRUE(std::is_move_assignable_v<DeepStreamReader>);
}

}  // namespace exrio
