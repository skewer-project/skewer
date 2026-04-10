#include <gtest/gtest.h>
#include <filesystem>

#include "exrio/deep_reader.h"
#include "exrio/deep_image.h"

constexpr float kEpsilon = 1e-6f;

class GoldenImageTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Get path to fixtures
        auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        testName_ = info->name();
        for (auto& c : testName_) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }

        // Build path to fixtures (adjust relative path as needed)
        // Will need to make this a bit more robust
        std::filesystem::path projectRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        fixturesDir_ = projectRoot / "tests" / "integration" / "fixtures";
        goldenImagePath_ = fixturesDir_ / "golden_images" / "all_features_512x512.exr"; // Make this an environment variable

        // Create temp dir for render output
        tempDir_ = std::filesystem::temp_directory_path() / "skewer_tests" / testName_;
        std::filesystem::create_directories(tempDir_);
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);  // Cleanup
    }

    // Helper: Compare two deep images pixel by pixel
    bool compareDeepImages(const exrio::DeepImage& a, const exrio::DeepImage& b, float epsilon = kEpsilon) {
        if (a.width() != b.width() || a.height() != b.height()) return false;

        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                auto& pixelA = a.pixel(x, y);
                auto& pixelB = b.pixel(x, y);

                if (pixelA.sampleCount() != pixelB.sampleCount()) return false;

                for (size_t i = 0; i < pixelA.sampleCount(); ++i) {
                    const auto& sampleA = pixelA[i];
                    const auto& sampleB = pixelB[i];

                    // Compare each field with epsilon tolerance
                    if (std::abs(sampleA.depth - sampleB.depth) > epsilon) return false;
                    if (std::abs(sampleA.depth_back - sampleB.depth_back) > epsilon) return false;
                    if (std::abs(sampleA.red - sampleB.red) > epsilon) return false;
                    if (std::abs(sampleA.green - sampleB.green) > epsilon) return false;
                    if (std::abs(sampleA.blue - sampleB.blue) > epsilon) return false;
                    if (std::abs(sampleA.alpha - sampleB.alpha) > epsilon) return false;
                }
            }
        }
        return true;
    }

    std::string testName_;
    std::filesystem::path fixturesDir_;
    std::filesystem::path goldenImagePath_;
    std::filesystem::path tempDir_;
};

TEST_F(GoldenImageTest, AllFeaturesSceneRendersIdentically) {
    // Render the scene
    std::string scenePath = (fixturesDir_ / "golden_scenes" / "all_features_scene.json").string();
    std::string outputPath = (tempDir_ / "test_render.exr").string();
    std::string renderCmd =
        "../../build/relwithdebinfo/skewer/skewer-render --scene " + scenePath +
        " --output " + outputPath + " --width 512 --height 512";

    int renderResult = system(renderCmd.c_str()); // this will perform the render
    ASSERT_EQ(renderResult, 0) << "Render failed";

    // Load rendered image and golden image
    ASSERT_TRUE(std::filesystem::exists(outputPath)) << "Render output not found";
    exrio::DeepImage renderedImage = exrio::loadDeepEXR(outputPath);

    ASSERT_TRUE(std::filesystem::exists(goldenImagePath_)) << "Golden image not found";
    exrio::DeepImage goldenImage = exrio::loadDeepEXR(goldenImagePath_);

    // Compare with golden image
    EXPECT_TRUE(compareDeepImages(renderedImage, goldenImage, kEpsilon))
        << "Rendered image differs from golden image - possible regression!";
}
