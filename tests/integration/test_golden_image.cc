#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "exrio/deep_image.h"
#include "exrio/deep_reader.h"
#include "picosha2.h"
#include "session/render_session.h"

#ifdef SKEWER_GENERATE_GOLDEN
static bool kGenerateGolden = true;  // Set via CMake option
#else
static bool kGenerateGolden = false;  // Default: compare against goldens
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------

/**
 * Compute a SHA-256 hex digest of the raw pixel data in a DeepImage.
 *
 * The hash covers: width, height, per-pixel sample counts, and every float
 * field (depth, depth_back, r, g, b, a) of every sample in row-major order.
 * This guarantees that any regression that changes geometry, colour, or
 * sample structure will be detected.
 */
static std::string computeDeepImageHash(const exrio::DeepImage& img) {
    picosha2::hash256_one_by_one hasher;

    // Helper: feed raw bytes of a POD value into the hasher.
    auto feedBytes = [&](const void* ptr, size_t len) {
        const auto* bytes = reinterpret_cast<const picosha2::byte_t*>(ptr);
        hasher.process(bytes, bytes + len);
    };

    // Hash dimensions so that two images with different sizes never collide.
    // may not be needed for now, but it's a good guardrail.
    int32_t dims[2] = {static_cast<int32_t>(img.width()), static_cast<int32_t>(img.height())};
    feedBytes(dims, sizeof(dims));

    // Hash every pixel's sample data, row-major.
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
// JSON manifest I/O
// ---------------------------------------------------------------------------

static fs::path hashManifestPath() {
    fs::path projectRoot = fs::path(__FILE__).parent_path().parent_path().parent_path();
    return projectRoot / "tests" / "integration" / "fixtures" / "golden_hashes.json";
}

/**
 * Read the golden_hashes.json manifest into a json object.
 * Returns an empty object if the file does not exist.
 */
static json loadHashManifest() {
    fs::path path = hashManifestPath();
    if (!fs::exists(path)) return json::object();
    std::ifstream ifs(path);
    return json::parse(ifs);
}

/**
 * Read a single hash from the manifest.  Returns "" if not found.
 */
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
static void upsertHash(const std::string& key, const std::string& hash) {
    json manifest = loadHashManifest();

    // Ensure top-level metadata fields exist.
    manifest["format_version"] = 1;
    manifest["hash_algorithm"] = "sha256";
    manifest["image_width"] = 800;
    manifest["image_height"] = 450;

    manifest["hashes"][key] = hash;

    std::ofstream ofs(hashManifestPath());
    ofs << manifest.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// The test
// ---------------------------------------------------------------------------

TEST_P(GoldenImageTest, RendersIdentically) {
    // Skip golden image tests in CI unless regenerating or explicitly enabled.
    if (std::getenv("CI") != nullptr && !kGenerateGolden &&
        std::getenv("SKEWER_RUN_GOLDEN") == nullptr) {
        GTEST_SKIP_("Golden image tests run in integration.yml workflow");
    }

    // Render the scene
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

    // Hash the rendered deep image
    exrio::DeepImage renderedImage = exrio::loadDeepEXR(outputPath.string());
    std::string renderedHash = computeDeepImageHash(renderedImage);

    if (kGenerateGolden) {
        // Generate mode: persist the hash to the manifest
        upsertHash(sceneFolder_, renderedHash);
        std::cout << "[GOLDEN-REGEN] " << sceneFolder_ << " => " << renderedHash << "\n";
    } else {
        // Check mode: compare against the committed manifest
        json manifest = loadHashManifest();
        std::string goldenHash = lookupHash(manifest, sceneFolder_);

        ASSERT_FALSE(goldenHash.empty())
            << "No golden hash found for '" << sceneFolder_
            << "' in golden_hashes.json. Run with SKEWER_GENERATE_GOLDEN=ON "
               "to generate.";

        EXPECT_EQ(renderedHash, goldenHash)
            << "Rendered image hash differs from golden hash — possible "
               "regression!\n"
            << "  scene:    " << sceneFolder_ << "\n"
            << "  expected: " << goldenHash << "\n"
            << "  actual:   " << renderedHash;
    }
}

INSTANTIATE_TEST_SUITE_P(MaterialTests, GoldenImageTest, ::testing::ValuesIn(kTestCases),
                         [](const testing::TestParamInfo<TestCase>& info) {
                             return info.param.name;
                         });
