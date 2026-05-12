#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"

// RunBatchMode composites a contiguous frame chunk as a Cloud Batch task.
//
// Required env vars (set by Cloud Workflows + Cloud Batch):
//   BATCH_TASK_INDEX     — 0-based chunk index (set automatically by Cloud Batch)
//   LAYER_URI_PREFIXES   — comma-separated GCS FUSE paths for each rendered layer
//   LAYER_MODES          — comma-separated modes ("static" or "animated"), one per layer
//   OUTPUT_URI_PREFIX    — GCS FUSE output directory for composited frames
//
// Optional env vars:
//   NUM_FRAMES           — total frames to composite
//   LOOM_FRAMES_PER_TASK — frames assigned to each composite task
//   LOOM_FRAME_PARALLELISM — max frames composited concurrently inside this task
//
// Static layers contribute static.exr to every frame.
// Animated layers contribute frame-NNNN.exr for the frame being composited.
static int ParsePositiveIntEnv(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value) return fallback;

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 1) {
        std::cerr << "[LOOM BATCH]: Invalid " << name << "=" << value << "\n";
        return -1;
    }
    return static_cast<int>(parsed);
}

static int ParseNonNegativeIntEnv(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value) return fallback;

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 0) {
        std::cerr << "[LOOM BATCH]: Invalid " << name << "=" << value << "\n";
        return -1;
    }
    return static_cast<int>(parsed);
}

static void CompositeFrame(int frame, const std::vector<std::string>& prefixes,
                           const std::vector<std::string>& modes, const std::string& output_prefix,
                           int row_thread_count, std::mutex& log_mutex) {
    char frame_str[8];
    std::snprintf(frame_str, sizeof(frame_str), "%04d", frame);

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

    std::string output_path = output_prefix + "frame-" + frame_str + ".exr";

    {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cout << "[LOOM BATCH]: Frame " << frame << " | " << input_files.size()
                  << " layers | row threads: " << row_thread_count << " | output: " << output_path
                  << "\n";
        for (size_t i = 0; i < input_files.size(); ++i) {
            std::cout << "[LOOM BATCH]:   layer[" << i << "] (" << modes[i]
                      << "): " << input_files[i] << "\n";
        }
    }

    std::vector<float> z_offsets(input_files.size() > 1 ? input_files.size() - 1 : 0, 0.0f);
    Options opts{input_files, z_offsets, ""};

    std::vector<std::unique_ptr<deep_compositor::DeepInfo>> imagesInfo;
    if (exrio::SaveImageInfo(opts, imagesInfo) == 1) {
        throw std::runtime_error("Failed to load image info");
    }

    int width = 0, height = 0;
    if (!imagesInfo.empty()) {
        width = imagesInfo[0]->width();
        height = imagesInfo[0]->height();
    }

    std::vector<float> flat_image =
        deep_compositor::ProcessAllEXR(opts, height, width, imagesInfo, row_thread_count);
    exrio::WriteFlatOutputs(flat_image, output_path, /*flatOutput=*/true, /*pngOutput=*/true, width,
                            height);

    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << "[LOOM BATCH]: Composite complete: " << output_path << "\n";
}

static int RunBatchMode() {
    const char* task_index_str = std::getenv("BATCH_TASK_INDEX");
    const char* layer_prefixes_env = std::getenv("LAYER_URI_PREFIXES");
    const char* layer_modes_env = std::getenv("LAYER_MODES");
    const char* num_frames_env = std::getenv("NUM_FRAMES");
    const char* output_prefix_env = std::getenv("OUTPUT_URI_PREFIX");

    if (!task_index_str || !layer_prefixes_env || !layer_modes_env || !output_prefix_env) {
        std::cerr << "[LOOM BATCH]: Missing required env vars (BATCH_TASK_INDEX, "
                     "LAYER_URI_PREFIXES, LAYER_MODES, OUTPUT_URI_PREFIX)\n";
        return 1;
    }

    const int task_index = ParseNonNegativeIntEnv("BATCH_TASK_INDEX", 0);
    const int frames_per_task = ParsePositiveIntEnv("LOOM_FRAMES_PER_TASK", 1);
    if (task_index < 0 || frames_per_task < 1) return 1;

    const int fallback_num_frames = task_index * frames_per_task + frames_per_task;
    const int num_frames = num_frames_env ? ParsePositiveIntEnv("NUM_FRAMES", fallback_num_frames)
                                          : fallback_num_frames;

    if (task_index < 0 || num_frames < 1 || frames_per_task < 1) {
        std::cerr << "[LOOM BATCH]: Invalid task/index config: BATCH_TASK_INDEX=" << task_index
                  << " NUM_FRAMES=" << num_frames << " LOOM_FRAMES_PER_TASK=" << frames_per_task
                  << "\n";
        return 1;
    }

    const int frame_start = task_index * frames_per_task + 1;
    const int frame_end = std::min(frame_start + frames_per_task - 1, num_frames);
    if (frame_start > num_frames) {
        std::cout << "[LOOM BATCH]: Chunk " << task_index << " starts after final frame; no work\n";
        return 0;
    }

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

    std::string output_prefix = output_prefix_env;
    if (!output_prefix.empty() && output_prefix.back() != '/') output_prefix += '/';

    const int frame_count = frame_end - frame_start + 1;
    const int hardware_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    int frame_parallelism =
        ParsePositiveIntEnv("LOOM_FRAME_PARALLELISM", std::min(frame_count, hardware_threads));
    if (frame_parallelism < 1) return 1;
    frame_parallelism = std::min(frame_parallelism, frame_count);

    const int row_thread_count = std::max(1, hardware_threads / frame_parallelism);

    std::cout << "[LOOM BATCH]: Chunk " << task_index << " | frames " << frame_start << "-"
              << frame_end << " | " << prefixes.size() << " layers | frame parallelism "
              << frame_parallelism << " | row threads/frame " << row_thread_count << "\n";

    std::atomic<int> next_frame{frame_start};
    std::atomic<bool> failed{false};
    std::mutex log_mutex;
    std::mutex error_mutex;
    std::string first_error;

    auto worker = [&]() {
        while (!failed.load()) {
            const int frame = next_frame.fetch_add(1);
            if (frame > frame_end) return;

            try {
                CompositeFrame(frame, prefixes, modes, output_prefix, row_thread_count, log_mutex);
            } catch (const std::exception& e) {
                failed.store(true);
                std::lock_guard<std::mutex> lock(error_mutex);
                if (first_error.empty()) {
                    first_error = "frame " + std::to_string(frame) + ": " + e.what();
                }
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(frame_parallelism));
    for (int i = 0; i < frame_parallelism; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& worker_thread : workers) {
        if (worker_thread.joinable()) worker_thread.join();
    }

    if (failed.load()) {
        std::cerr << "[LOOM BATCH]: Composite failed for " << first_error << "\n";
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
