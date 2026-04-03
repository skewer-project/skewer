/// Cloud Run / one-shot Loom worker: reads composite task from env, writes output, reports via
/// gRPC.

#include <coordinator_worker/worker_loop.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"

namespace {

std::optional<int> EnvInt(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return std::nullopt;
    }
    try {
        return std::stoi(v);
    } catch (...) {
        return std::nullopt;
    }
}

std::string RequiredEnv(const char* name) {
    const char* v = std::getenv(name);
    if (!v) {
        return {};
    }
    return v;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const std::string task_id = RequiredEnv("TASK_ID");
    const std::string job_id = RequiredEnv("JOB_ID");
    const std::string output_uri = RequiredEnv("OUTPUT_URI");
    if (task_id.empty() || job_id.empty() || output_uri.empty()) {
        std::cerr << "[LOOM] Missing required env: TASK_ID, JOB_ID, OUTPUT_URI\n";
        return 2;
    }

    auto layer_count = EnvInt("LAYER_COUNT");
    if (!layer_count || *layer_count <= 0) {
        std::cerr << "[LOOM] LAYER_COUNT must be a positive integer\n";
        return 2;
    }

    std::vector<std::string> input_files;
    input_files.reserve(static_cast<size_t>(*layer_count));
    for (int i = 0; i < *layer_count; ++i) {
        std::string key = "LAYER_URI_" + std::to_string(i);
        std::string path = RequiredEnv(key.c_str());
        if (path.empty()) {
            std::cerr << "[LOOM] Missing " << key << "\n";
            return 2;
        }
        input_files.push_back(std::move(path));
    }

    std::string coord = coordinator_worker::DefaultCoordinatorAddr();
    if (const char* c = std::getenv("COORDINATOR_ADDR"); c && *c) {
        coord = c;
    }

    std::string worker_id = RequiredEnv("WORKER_ID");
    if (worker_id.empty()) {
        worker_id = coordinator_worker::MakeWorkerId("loom-cloud-worker");
    }

    const auto t0 = std::chrono::steady_clock::now();

    coordinator_worker::TaskOutcome outcome;
    outcome.output_uri = output_uri;
    outcome.success = true;

    try {
        Options opts = {
            input_files,
            std::vector<float>{0},
            "",
        };

        std::vector<std::unique_ptr<deep_compositor::DeepInfo>> images_info;
        int load_rc = exrio::SaveImageInfo(opts, images_info);
        if (load_rc == 1) {
            outcome.success = false;
            outcome.error_message = "Failed to load input EXR files";
        } else {
            // Read dimensions from the first loaded image; env vars WIDTH/HEIGHT
            // are optional overrides (useful when bypassing EXR header inspection).
            int w = images_info[0]->width();
            int h = images_info[0]->height();
            if (auto ew = EnvInt("WIDTH"); ew && *ew > 0) w = *ew;
            if (auto eh = EnvInt("HEIGHT"); eh && *eh > 0) h = *eh;

            std::vector<float> final_image =
                deep_compositor::ProcessAllEXR(opts, h, w, images_info);
            exrio::WriteFlatOutputs(final_image, outcome.output_uri, opts.flat_output,
                                    opts.png_output, w, h);
        }
    } catch (const std::exception& e) {
        outcome.success = false;
        outcome.error_message = e.what();
        std::cerr << "[LOOM] Task failed: " << outcome.error_message << "\n";
    }

    const auto t1 = std::chrono::steady_clock::now();
    const int64_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (!coordinator_worker::ReportTaskResult(coord, worker_id, task_id, job_id, outcome,
                                              elapsed_ms)) {
        return outcome.success ? 3 : 4;
    }

    return outcome.success ? 0 : 1;
}
