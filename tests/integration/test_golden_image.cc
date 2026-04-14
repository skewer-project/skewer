#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "exrio/deep_image.h"
#include "exrio/deep_reader.h"
#include "session/render_session.h"

constexpr float kEpsilon = 1e-1f;  // Very relaxed for ARM/x86 differences
#ifdef SKEWER_GENERATE_GOLDEN
static bool kGenerateGolden = true;  // Set via CMake option
#else
static bool kGenerateGolden = false;  // Default: compare against goldens
#endif

namespace fs = std::filesystem;

struct TestCase {
    std::string name;
    std::string scene_folder;
};

static const std::vector<TestCase> kTestCases = {
    {"MatLambertianRed", "mat_lambertian_red"},
    {"MatMetalGold", "mat_metal_gold"},
    {"MatMetalMirror", "mat_metal_mirror"},
    {"MatMetalRough", "mat_metal_rough"},
    {"MatGlassClear", "mat_glass_clear"},
    {"MatGlassDiamond", "mat_glass_diamond"},
    {"MatAllMixed", "mat_all_mixed"},
    {"LightSinglePoint", "light_single_point"},
    {"LightDualPoint", "light_dual_point"},
    {"LightAreaEmissive", "light_area_emissive"},
    {"LightHardShadow", "light_hard_shadow"},
    {"LightSoftShadow", "light_soft_shadow"},
    {"LightBacklight", "light_backlight"},
    {"VolWDASCloud", "vol_wdas_cloud"},
    {"VolCloud05", "vol_cloud_05"},
};

class GoldenImageTest : public ::testing::TestWithParam<TestCase> {
  protected:
    void SetUp() override {
        const auto& param = GetParam();
        testName_ = param.name;
        sceneFolder_ = param.scene_folder;

        fs::path projectRoot = fs::path(__FILE__).parent_path().parent_path().parent_path();
        fixturesDir_ = projectRoot / "tests" / "integration" / "fixtures";
        goldenImagePath_ = fixturesDir_ / "golden_images" / (sceneFolder_ + "_800x450.exr");

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
    std::string sceneFolder_;
    fs::path fixturesDir_;
    fs::path goldenImagePath_;
    fs::path tempDir_;
};

TEST_P(GoldenImageTest, RendersIdentically) {
    // Skip golden image tests in CI unless regenerating or explicitly enabled.
    if (std::getenv("CI") != nullptr && !kGenerateGolden &&
        std::getenv("SKEWER_RUN_GOLDEN") == nullptr) {
        GTEST_SKIP_("Golden image tests run in integration.yml workflow");
    }

    fs::path scenePath = fixturesDir_ / "golden_scenes" / sceneFolder_ / "scene.json";
    fs::path outputPath = tempDir_ / "test_render.exr";
    fs::path pngPath = tempDir_ / "test_render.png";

    skwr::RenderSession session;
    session.LoadSceneFromFile(scenePath.string(), 0);

    session.Options().image_config.outfile = pngPath.string();
    session.Options().image_config.exrfile = outputPath.string();
    session.Options().image_config.width = 800;
    session.Options().image_config.height = 450;
    session.Options().integrator_config.max_samples = 256;
    session.Options().integrator_config.noise_threshold = 0.075f;
    session.Options().integrator_config.num_threads = 1;
    session.Options().integrator_config.enable_deep = true;
    session.RebuildFilm();

    session.Render();
    session.Save();

    bool firstRun = !fs::exists(goldenImagePath_) || kGenerateGolden;
    if (firstRun) {
        fs::copy_file(outputPath, goldenImagePath_, fs::copy_options::overwrite_existing);
        fs::path pngGolden = fixturesDir_ / "golden_images" / (sceneFolder_ + "_800x450.png");
        if (fs::exists(pngPath)) {
            fs::copy_file(pngPath, pngGolden, fs::copy_options::overwrite_existing);
        }
    }

    ASSERT_TRUE(fs::exists(outputPath)) << "Render output not found";
    exrio::DeepImage renderedImage = exrio::loadDeepEXR(outputPath.string());

    ASSERT_TRUE(fs::exists(goldenImagePath_)) << "Golden image not found";
    exrio::DeepImage goldenImage = exrio::loadDeepEXR(goldenImagePath_.string());

    EXPECT_TRUE(compareDeepImages(renderedImage, goldenImage, kEpsilon))
        << "Rendered image differs from golden image - possible regression!";
}

INSTANTIATE_TEST_SUITE_P(MaterialTests, GoldenImageTest, ::testing::ValuesIn(kTestCases),
                         [](const testing::TestParamInfo<TestCase>& info) {
                             return info.param.name;
                         });
