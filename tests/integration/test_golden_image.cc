#include <gtest/gtest.h>

#include <filesystem>

#include "exrio/deep_image.h"
#include "exrio/deep_reader.h"
#include "session/render_session.h"

constexpr float kEpsilon = 1e-6f;
const std::string kGoldenImageName = "all_features_512x512.exr";

namespace fs = std::filesystem;

class GoldenImageTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        testName_ = info->name();
        for (auto& c : testName_) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }

        fs::path projectRoot = fs::path(__FILE__).parent_path().parent_path().parent_path();
        fixturesDir_ = projectRoot / "tests" / "integration" / "fixtures";
        goldenImagePath_ = fixturesDir_ / "golden_images" / kGoldenImageName;

        tempDir_ = fs::temp_directory_path() / "skewer_tests" / testName_;
        fs::create_directories(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    bool compareDeepImages(const exrio::DeepImage& a, const exrio::DeepImage& b,
                           float epsilon = kEpsilon) {
        if (a.width() != b.width() || a.height() != b.height()) return false;

        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                auto& pixelA = a.pixel(x, y);
                auto& pixelB = b.pixel(x, y);

                if (pixelA.sampleCount() != pixelB.sampleCount()) return false;

                for (size_t i = 0; i < pixelA.sampleCount(); ++i) {
                    const auto& sampleA = pixelA[i];
                    const auto& sampleB = pixelB[i];

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
    fs::path fixturesDir_;
    fs::path goldenImagePath_;
    fs::path tempDir_;
};

static bool generate_golden = false;

TEST_F(GoldenImageTest, AllFeaturesSceneRendersIdentically) {
    // Render the golden scene
    fs::path scenePath = fixturesDir_ / "golden_scenes" / "test_single.scene.json";
    fs::path outputPath = tempDir_ / "test_render.exr";
    fs::path pngPath = tempDir_ / "test_render.png";

    skwr::RenderSession session;
    session.LoadSceneFromFile(scenePath.string(), 0);

    session.Options().image_config.outfile = pngPath.string();
    session.Options().image_config.exrfile = outputPath.string();
    session.Options().image_config.width = 512;
    session.Options().image_config.height = 512;
    session.Options().integrator_config.enable_deep = true;
    session.RebuildFilm();

    session.Render();
    session.Save();

    // Replace golden image if first run or generate_golden is set
    bool firstRun = !fs::exists(goldenImagePath_);
    if (firstRun || generate_golden) {
        fs::copy_file(outputPath, goldenImagePath_, fs::copy_options::overwrite_existing);
        fs::path pngGoldenPath = fixturesDir_ / "golden_images" / "all_features_512x512.png";
        if (fs::exists(pngPath)) {
            fs::copy_file(pngPath, pngGoldenPath, fs::copy_options::overwrite_existing);
        }
    }

    // Check if rendered images exist before comparing
    ASSERT_TRUE(fs::exists(outputPath)) << "Render output not found";
    exrio::DeepImage renderedImage = exrio::loadDeepEXR(outputPath.string());

    ASSERT_TRUE(fs::exists(goldenImagePath_)) << "Golden image not found";
    exrio::DeepImage goldenImage = exrio::loadDeepEXR(goldenImagePath_.string());

    // Compare rendered image with golden image
    EXPECT_TRUE(compareDeepImages(renderedImage, goldenImage, kEpsilon))
        << "Rendered image differs from golden image - possible regression!";
}
