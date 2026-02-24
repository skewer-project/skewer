#include <gtest/gtest.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include "deep_image.h"
#include "deep_reader.h"
#include "deep_writer.h"
#include "../test_helpers.h"

using namespace deep_compositor;
namespace fs = std::filesystem;

// ============================================================================
// IORoundtripTest fixture
// ============================================================================

class IORoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use std::filesystem to get a reliable, writable temp directory.
        // Create a per-test subdirectory so parallel or repeated runs don't collide.
        auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string dirName = std::string(info->name());
        for (auto& c : dirName) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }
        testDir_ = fs::temp_directory_path() / "dc_tests" / dirName;
        fs::create_directories(testDir_);
        tempDir_ = testDir_.string();
        if (tempDir_.back() != '/' && tempDir_.back() != '\\') {
            tempDir_ += '/';
        }
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(testDir_, ec);  // best-effort cleanup; ignore errors
    }

    std::string tempPath(const std::string& name) {
        return tempDir_ + name;
    }

    // Write a known deep image and load it back
    DeepImage roundtrip(const DeepImage& img, const std::string& filename) {
        writeDeepEXR(img, filename);
        return loadDeepEXR(filename);
    }

    fs::path testDir_;
    std::string tempDir_;
};

// ============================================================================
// Roundtrip correctness tests
// ============================================================================

TEST_F(IORoundtripTest, WriteAndReadPreservesDimensions) {
    DeepImage img(5, 7);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    std::string path = tempPath("dimensions.exr");
    DeepImage loaded = roundtrip(img, path);
    EXPECT_EQ(loaded.width(), 5);
    EXPECT_EQ(loaded.height(), 7);
}

TEST_F(IORoundtripTest, WriteAndReadPreservesSampleCount) {
    DeepImage img(2, 2);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    img.pixel(0, 0).addSample(makePoint(2.0f, 0.3f, 0.3f, 0.3f, 0.6f));
    img.pixel(1, 1).addSample(makePoint(3.0f, 0.7f, 0.7f, 0.7f, 0.5f));
    std::string path = tempPath("sample_count.exr");
    DeepImage loaded = roundtrip(img, path);
    EXPECT_EQ(loaded.pixel(0, 0).sampleCount(), 2u);
    EXPECT_EQ(loaded.pixel(1, 1).sampleCount(), 1u);
    EXPECT_EQ(loaded.pixel(0, 1).sampleCount(), 0u);
}

TEST_F(IORoundtripTest, WriteAndReadPreservesDepthValues) {
    DeepImage img(1, 1);
    img.pixel(0, 0).addSample(makePoint(3.14f, 0.5f, 0.5f, 0.5f, 0.8f));
    std::string path = tempPath("depth_values.exr");
    DeepImage loaded = roundtrip(img, path);
    ASSERT_EQ(loaded.pixel(0, 0).sampleCount(), 1u);
    EXPECT_NEAR(loaded.pixel(0, 0)[0].depth, 3.14f, 1e-6f);
}

TEST_F(IORoundtripTest, WriteAndReadPreservesRGBAValues) {
    DeepImage img(1, 1);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.25f, 0.5f, 0.75f, 0.6f));
    std::string path = tempPath("rgba_values.exr");
    DeepImage loaded = roundtrip(img, path);
    ASSERT_EQ(loaded.pixel(0, 0).sampleCount(), 1u);
    const DeepSample& s = loaded.pixel(0, 0)[0];
    EXPECT_NEAR(s.red,   0.25f, 1e-6f);
    EXPECT_NEAR(s.green, 0.5f,  1e-6f);
    EXPECT_NEAR(s.blue,  0.75f, 1e-6f);
    EXPECT_NEAR(s.alpha, 0.6f,  1e-6f);
}

TEST_F(IORoundtripTest, WriteAndReadPreservesVolumetricDepthBack) {
    DeepImage img(1, 1);
    img.pixel(0, 0).addSample(makeVolume(1.0f, 4.5f, 0.5f, 0.5f, 0.5f, 0.8f));
    std::string path = tempPath("depth_back.exr");
    DeepImage loaded = roundtrip(img, path);
    ASSERT_EQ(loaded.pixel(0, 0).sampleCount(), 1u);
    EXPECT_NEAR(loaded.pixel(0, 0)[0].depth,      1.0f, 1e-6f);
    EXPECT_NEAR(loaded.pixel(0, 0)[0].depth_back, 4.5f, 1e-6f);
}

TEST_F(IORoundtripTest, WriteAndReadPreservesDepthSortOrder) {
    DeepImage img(1, 1);
    img.pixel(0, 0).addSample(makePoint(3.0f, 0.5f, 0.5f, 0.5f, 0.5f));
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.5f));
    img.pixel(0, 0).addSample(makePoint(2.0f, 0.5f, 0.5f, 0.5f, 0.5f));
    std::string path = tempPath("sort_order.exr");
    DeepImage loaded = roundtrip(img, path);
    EXPECT_TRUE(loaded.isValid());
}

// ============================================================================
// Error handling tests
// ============================================================================

TEST_F(IORoundtripTest, LoadNonExistentFileThrowsDeepReaderException) {
    EXPECT_THROW(
        loadDeepEXR(tempPath("does_not_exist_xyz.exr")),
        DeepReaderException
    );
}

TEST_F(IORoundtripTest, IsDeepEXRReturnsTrueForWrittenFile) {
    DeepImage img(1, 1);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    std::string path = tempPath("is_deep.exr");
    writeDeepEXR(img, path);
    EXPECT_TRUE(isDeepEXR(path));
}

TEST_F(IORoundtripTest, IsDeepEXRReturnsFalseForNonExistentFile) {
    EXPECT_FALSE(isDeepEXR(tempPath("definitely_not_here.exr")));
}

TEST_F(IORoundtripTest, GetDeepEXRInfoReturnsCorrectDimensions) {
    DeepImage img(6, 9);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    std::string path = tempPath("info.exr");
    writeDeepEXR(img, path);
    int w = 0, h = 0;
    bool isDeep = false;
    bool ok = getDeepEXRInfo(path, w, h, isDeep);
    EXPECT_TRUE(ok);
    EXPECT_EQ(w, 6);
    EXPECT_EQ(h, 9);
    EXPECT_TRUE(isDeep);
}

TEST_F(IORoundtripTest, WriteDeepEXRWithZeroDimensionsThrows) {
    DeepImage img(0, 0);
    EXPECT_THROW(
        writeDeepEXR(img, tempPath("zero_dim.exr")),
        DeepWriterException
    );
}

TEST_F(IORoundtripTest, WriteFlatEXRDoesNotThrowForValidImage) {
    DeepImage img(4, 4);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.5f, 0.5f, 0.5f, 0.8f));
    std::string path = tempPath("flat.exr");
    EXPECT_NO_THROW(writeFlatEXR(img, path));
}

TEST_F(IORoundtripTest, FlattenAndWriteRoundtripFileExists) {
    DeepImage img(2, 2);
    img.pixel(0, 0).addSample(makePoint(1.0f, 0.8f, 0.4f, 0.2f, 0.9f));
    std::string path = tempPath("flat_roundtrip.exr");
    writeFlatEXR(img, path);
    // Verify file was created
    std::ifstream f(path);
    EXPECT_TRUE(f.good());
}
