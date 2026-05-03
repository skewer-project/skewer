#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"

// RunBatchMode composites a single frame as a Cloud Batch task.
//
// Required env vars (set by Cloud Workflows + Cloud Batch):
//   BATCH_TASK_INDEX     — 0-based frame index (set automatically by Cloud Batch)
//   LAYER_URI_PREFIXES   — comma-separated GCS FUSE paths for each rendered layer
//   LAYER_MODES          — comma-separated modes ("static" or "animated"), one per layer
//   OUTPUT_URI_PREFIX    — GCS FUSE output directory for composited frames
//
// Static layers contribute static.exr to every frame.
// Animated layers contribute frame-NNNN.exr for the frame being composited.
static int RunBatchMode() {
    const char* task_index_str = std::getenv("BATCH_TASK_INDEX");
    const char* layer_prefixes_env = std::getenv("LAYER_URI_PREFIXES");
    const char* layer_modes_env = std::getenv("LAYER_MODES");
    const char* output_prefix_env = std::getenv("OUTPUT_URI_PREFIX");

    if (!task_index_str || !layer_prefixes_env || !layer_modes_env || !output_prefix_env) {
        std::cerr << "[LOOM BATCH]: Missing required env vars (BATCH_TASK_INDEX, "
                     "LAYER_URI_PREFIXES, LAYER_MODES, OUTPUT_URI_PREFIX)\n";
        return 1;
    }

    int frame = std::atoi(task_index_str) + 1;  // BATCH_TASK_INDEX is 0-based; frames are 1-based
    char frame_str[8];
    std::snprintf(frame_str, sizeof(frame_str), "%04d", frame);

    // Parse comma-separated layer prefixes and modes in parallel.
    auto split_csv = [](const char* env) -> std::vector<std::string> {
        std::vector<std::string> parts;
        std::istringstream ss(env);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) parts.push_back(token);
        }
        return parts;
    };

    std::vector<std::string> prefixes = split_csv(layer_prefixes_env);
    std::vector<std::string> modes = split_csv(layer_modes_env);

    if (prefixes.empty()) {
        std::cerr << "[LOOM BATCH]: No input layers derived from LAYER_URI_PREFIXES\n";
        return 1;
    }
    if (modes.size() != prefixes.size()) {
        std::cerr << "[LOOM BATCH]: LAYER_MODES count (" << modes.size()
                  << ") does not match LAYER_URI_PREFIXES count (" << prefixes.size() << ")\n";
        return 1;
    }

    // Build per-frame input file list.
    // Static layers always use static.exr; animated layers use frame-NNNN.exr.
    std::vector<std::string> input_files;
    for (size_t i = 0; i < prefixes.size(); ++i) {
        std::string prefix = prefixes[i];
        if (!prefix.empty() && prefix.back() != '/') prefix += '/';

        if (modes[i] == "static") {
            input_files.push_back(prefix + "static.exr");
        } else {
            input_files.push_back(prefix + "frame-" + frame_str + ".exr");
        }
    }

    std::string output_prefix = output_prefix_env;
    if (!output_prefix.empty() && output_prefix.back() != '/') output_prefix += '/';
    std::string output_path = output_prefix + "frame-" + frame_str + ".exr";

    std::cout << "[LOOM BATCH]: Frame " << frame << " | " << input_files.size()
              << " layers | output: " << output_path << "\n";
    for (size_t i = 0; i < input_files.size(); ++i) {
        std::cout << "[LOOM BATCH]:   layer[" << i << "] (" << modes[i] << "): " << input_files[i]
                  << "\n";
    }

    try {
        std::vector<float> z_offsets(input_files.size() > 1 ? input_files.size() - 1 : 0, 0.0f);
        Options opts{input_files, z_offsets, ""};

        std::vector<std::unique_ptr<deep_compositor::DeepInfo>> imagesInfo;
        if (exrio::SaveImageInfo(opts, imagesInfo) == 1) {
            std::cerr << "[LOOM BATCH]: Failed to load image info\n";
            return 1;
        }

        int width = 0, height = 0;
        if (!imagesInfo.empty()) {
            width = imagesInfo[0]->width();
            height = imagesInfo[0]->height();
        }

        std::vector<float> flat_image =
            deep_compositor::ProcessAllEXR(opts, height, width, imagesInfo);
        exrio::WriteFlatOutputs(flat_image, output_path, /*flatOutput=*/true, /*pngOutput=*/true,
                                width, height);

        std::cout << "[LOOM BATCH]: Composite complete: " << output_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[LOOM BATCH]: Composite failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!std::getenv("BATCH_TASK_INDEX")) {
        std::cerr << "[LOOM BATCH]: BATCH_TASK_INDEX not set. "
                     "This binary runs as a Cloud Batch task only.\n";
        return 1;
    }

    return RunBatchMode();
}
