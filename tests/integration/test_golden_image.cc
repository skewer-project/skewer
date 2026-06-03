#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>
#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"
#include "deep_options.h"
#include "exrio/deep_image.h"
#include "exrio/deep_reader.h"
#include "picosha2.h"
#include "session/render_session.h"

#ifdef SKEWER_GENERATE_GOLDEN
static constexpr bool kGenerateGolden = true;
#else
static constexpr bool kGenerateGolden = false;
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Shared path / manifest helpers
// ---------------------------------------------------------------------------

static fs::path projectRoot() {
    return fs::path(__FILE__).parent_path().parent_path().parent_path();
}

static fs::path fixturesRoot() {
    return projectRoot() / "tests" / "integration" / "fixtures";
}

static fs::path productFixturesDir(const std::string& product) {
    return fixturesRoot() / product;
}

static fs::path hashManifestPath(const std::string& product) {
    return productFixturesDir(product) / "golden_hashes.json";
}

static bool shouldSkipGoldenInCi() {
    return std::getenv("CI") != nullptr && !kGenerateGolden &&
           std::getenv("SKEWER_RUN_GOLDEN") == nullptr;
}

/**
 * Read a golden_hashes.json manifest into a json object.
 * Returns an empty object if the file does not exist.
 */
static json loadHashManifest(const fs::path& path) {
    if (!fs::exists(path)) return json::object();
    std::ifstream ifs(path);
    if (!ifs.is_open()) return json::object();
    try {
        return json::parse(ifs);
    } catch (const json::parse_error& e) {
        ADD_FAILURE() << "Failed to parse " << path << ": " << e.what();
        return json::object();
    }
}

static std::string lookupHash(const json& manifest, const std::string& key) {
    if (!manifest.contains("hashes")) return "";
    const auto& hashes = manifest["hashes"];
    if (!hashes.contains(key)) return "";
    return hashes[key].get<std::string>();
}

/**
 * Write / update a hash in the manifest and flush to disk.
 * Thread-safety is not a concern: gtest runs parameterised cases
 * sequentially within a single binary.
 */
static void upsertHash(const fs::path& path, const std::string& key, const std::string& hash,
                       int width, int height) {
    json manifest = loadHashManifest(path);

    fs::create_directories(path.parent_path());
    manifest["format_version"] = 1;
    manifest["hash_algorithm"] = "sha256";
    manifest["image_width"] = width;
    manifest["image_height"] = height;
    manifest["hashes"][key] = hash;

    std::ofstream ofs(path);
    ofs << manifest.dump(2) << "\n";
}

static void verifyOrRegenerateHash(const fs::path& manifestPath, const std::string& sceneKey,
                                   const std::string& renderedHash, int width, int height) {
    if (kGenerateGolden) {
        upsertHash(manifestPath, sceneKey, renderedHash, width, height);
        std::cout << "[GOLDEN-REGEN] " << sceneKey << " => " << renderedHash << "\n";
        return;
    }

    json manifest = loadHashManifest(manifestPath);
    std::string goldenHash = lookupHash(manifest, sceneKey);

    ASSERT_FALSE(goldenHash.empty())
        << "No golden hash found for '" << sceneKey << "' in " << manifestPath
        << ". Run with SKEWER_GENERATE_GOLDEN=ON to generate.";

    EXPECT_EQ(renderedHash, goldenHash)
        << "Rendered image hash differs from golden hash - possible regression!\n"
        << "  scene:    " << sceneKey << "\n"
        << "  expected: " << goldenHash << "\n"
        << "  actual:   " << renderedHash;
}

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------

/**
 * Compute a SHA-256 hex digest of the raw pixel data in a DeepImage.
 */
static std::string computeDeepImageHash(const exrio::DeepImage& img) {
    picosha2::hash256_one_by_one hasher;

    auto feedBytes = [&](const void* ptr, size_t len) {
        const auto* bytes = reinterpret_cast<const picosha2::byte_t*>(ptr);
        hasher.process(bytes, bytes + len);
    };

    int32_t dims[2] = {static_cast<int32_t>(img.width()), static_cast<int32_t>(img.height())};
    feedBytes(dims, sizeof(dims));

    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const auto& px = img.pixel(x, y);
            uint32_t count = static_cast<uint32_t>(px.sampleCount());
            feedBytes(&count, sizeof(count));
            for (size_t i = 0; i < px.sampleCount(); ++i) {
                const auto& s = px[i];
                float vals[6] = {s.depth, s.depth_back, s.red, s.green, s.blue, s.alpha};
                feedBytes(vals, sizeof(vals));
            }
        }
    }

    hasher.finish();
    return picosha2::get_hash_hex_string(hasher);
}

// ---------------------------------------------------------------------------
// Skewer golden tests
// ---------------------------------------------------------------------------

struct TestCase {
    std::string name;
    std::string scene_folder;
};

static const std::vector<TestCase> kSkewerTestCases = {
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

class SkewerGoldenImageTest : public ::testing::TestWithParam<TestCase> {
  protected:
    void SetUp() override {
        const auto& param = GetParam();
        testName_ = param.name;
        sceneFolder_ = param.scene_folder;

        fixturesDir_ = productFixturesDir("skewer");
        tempDir_ = fs::temp_directory_path() / "skewer_tests" / testName_;
        fs::create_directories(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    std::string testName_;
    std::string sceneFolder_;
    fs::path fixturesDir_;
    fs::path tempDir_;
};

TEST_P(SkewerGoldenImageTest, RendersIdentically) {
    if (shouldSkipGoldenInCi()) {
        GTEST_SKIP_("Golden hash tests run in integration.yml workflow");
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

    ASSERT_TRUE(fs::exists(outputPath)) << "Render output not found";

    exrio::DeepImage renderedImage = exrio::loadDeepEXR(outputPath.string());
    std::string renderedHash = computeDeepImageHash(renderedImage);

    verifyOrRegenerateHash(hashManifestPath("skewer"), sceneFolder_, renderedHash,
                           renderedImage.width(), renderedImage.height());
}

INSTANTIATE_TEST_SUITE_P(SkewerScenes, SkewerGoldenImageTest,
                         ::testing::ValuesIn(kSkewerTestCases),
                         [](const testing::TestParamInfo<TestCase>& info) {
                             return info.param.name;
                         });

// ---------------------------------------------------------------------------
// Loom golden tests
// ---------------------------------------------------------------------------

struct LoomTestCase {
    std::string name;
    std::string input_folder;
    float merge_threshold;
};

static const std::vector<LoomTestCase> kLoomTestCases = {
    {"BallFog", "ball_fog", 0.001f},
    {"CollidingObjects", "colliding_balls", 0.001f},
};

class LoomGoldenImageTest : public ::testing::TestWithParam<LoomTestCase> {
  protected:
    void SetUp() override {
        const auto& param = GetParam();
        testName_ = param.name;
        inputFolder_ = param.input_folder;
        mergeThreshold_ = param.merge_threshold;

        fixturesDir_ = productFixturesDir("loom");
        inputDir_ = fixturesDir_ / "golden_inputs" / inputFolder_;
        tempDir_ = fs::temp_directory_path() / "loom_tests" / testName_;
        fs::create_directories(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    std::string testName_;
    std::string inputFolder_;
    fs::path inputDir_;
    float mergeThreshold_;
    fs::path fixturesDir_;
    fs::path tempDir_;
};

TEST_P(LoomGoldenImageTest, CompositesIdentically) {
    if (shouldSkipGoldenInCi()) {
        GTEST_SKIP_("Golden hash tests run in integration.yml workflow");
    }

    Options opts;
    opts.output_prefix = (tempDir_ / "loom_render").string();
    opts.deep_output = true;
    opts.flat_output = false;
    opts.png_output = false;
    opts.verbose = false;
    opts.merge_threshold = mergeThreshold_;
    opts.show_help = false;
    opts.mod_offset = false;
    opts.enable_merging = true;

    ASSERT_TRUE(fs::exists(inputDir_)) << "Loom input folder not found: " << inputDir_;
    std::vector<fs::path> inputFiles;
    for (const auto& input : fs::directory_iterator(inputDir_)) {
        if (!input.is_regular_file() || input.path().extension() != ".exr") continue;
        inputFiles.push_back(input.path());
    }
    std::sort(inputFiles.begin(), inputFiles.end());

    ASSERT_FALSE(inputFiles.empty()) << "No Loom input EXRs found in: " << inputDir_;
    for (const auto& input : inputFiles) {
        ASSERT_TRUE(fs::exists(input)) << "Loom input EXR not found: " << input;
        opts.input_files.push_back(input.string());
    }

    std::vector<std::unique_ptr<deep_compositor::DeepInfo>> imagesInfo;
    ASSERT_EQ(exrio::SaveImageInfo(opts, imagesInfo), 0);
    ASSERT_FALSE(imagesInfo.empty());

    int height = imagesInfo[0]->height();
    int width = imagesInfo[0]->width();
    ASSERT_NO_THROW(deep_compositor::ProcessAllEXR(opts, height, width, imagesInfo, 1));

    fs::path outputPath = opts.output_prefix + "_merged.exr";
    ASSERT_TRUE(fs::exists(outputPath)) << "Loom merged output not found";

    exrio::DeepImage mergedImage = exrio::loadDeepEXR(outputPath.string());
    std::string renderedHash = computeDeepImageHash(mergedImage);

    verifyOrRegenerateHash(hashManifestPath("loom"), inputFolder_, renderedHash,
                           mergedImage.width(), mergedImage.height());
}

INSTANTIATE_TEST_SUITE_P(LoomScenes, LoomGoldenImageTest, ::testing::ValuesIn(kLoomTestCases),
                         [](const testing::TestParamInfo<LoomTestCase>& info) {
                             return info.param.name;
                         });
