#include <gtest/gtest.h>

#include <fstream>
#include <streambuf>
#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"
#include "deep_options.h"

namespace fs = std::filesystem;

class StressTest : public ::testing::Test {
  protected:
  // Simple options for testing a loom instance
  Options simple_opts = {
      .input_files = {},
      .input_z_offsets = {},
      .output_prefix = "",
      .deep_output = false,
      .flat_output = true,
      .png_output = true,
      .verbose = false,
      .merge_threshold = 0.001f,
      .show_help = false,
      .mod_offset = false,
      .enable_merging = true
  };

  fs::path current_file = __FILE__;
  fs::path tests_dir = current_file.parent_path().parent_path();
  fs::path assets_dir = tests_dir / "assets";
};

// ============================================================================
// Many Objects in smoke to test volumetric splitting
// ============================================================================

TEST_F(StressTest, VolumetricSplittingStress) {
    Options opts = simple_opts;
    std::vector<std::string> input_files = {assets_dir / "layer_fog.exr", assets_dir / "layer_objects.exr"};
    opts.input_files = input_files;

    std::vector<std::unique_ptr<deep_compositor::DeepInfo>> imagesInfo;
    ASSERT_EQ(exrio::SaveImageInfo(opts, imagesInfo), 0);

    int height = imagesInfo[0]->height();
    int width = imagesInfo[0]->width();
    ASSERT_NO_FATAL_FAILURE(deep_compositor::ProcessAllEXR(opts, height, width, imagesInfo));
}
