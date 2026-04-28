#include <gtest/gtest.h>

#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"
#include "deep_options.h"

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
};

// ============================================================================
// Many Objects in smoke to test volumetric splitting
// ============================================================================

TEST_F(StressTest, VolumetricSplittingStress) {
    Options opts = simple_opts;
    std::vector<std::string> input_files = {"../assets/layer_fog.exr", "../assets/layer_objects.exr"};
    opts.input_files = input_files;

    std::vector<std::unique_ptr<deep_compositor::DeepInfo>> imagesInfo;
    ASSERT_FALSE(exrio::SaveImageInfo(opts, imagesInfo));

    int height = imagesInfo[0]->height();
    int width = imagesInfo[0]->width();
    ASSERT_NO_FATAL_FAILURE(deep_compositor::ProcessAllEXR(opts, height, width, imagesInfo));
}
