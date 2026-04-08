#include <coordinator_worker/worker_loop.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"
#include "proto/coordinator/v1/coordinator.pb.h"

using api::proto::coordinator::v1::CompositeTask;
using api::proto::coordinator::v1::GetWorkStreamResponse;

namespace {

/// Load deep EXRs, composite, and write flat/deep outputs described by `task`.
coordinator_worker::TaskOutcome RunCompositePipeline(const CompositeTask& task) {
    coordinator_worker::TaskOutcome out;
    out.success = true;
    out.output_uri = task.output_uri();

    std::vector<std::string> input_files(task.layer_uris().begin(), task.layer_uris().end());

    std::cout << "[Loom] -> Composite Inputs (" << input_files.size() << " files):\n";
    for (const auto& uri : input_files) {
        std::cout << "  - " << uri << "\n";
    }

    Options opts = {
        input_files,
        std::vector<float>{0},  // TODO: collect real input_z_offsets
        "",                     // coordinator handles output prefixing and parsing
    };

    std::vector<std::unique_ptr<deep_compositor::DeepInfo>> images_info;
    int load_rc = exrio::SaveImageInfo(opts, images_info);
    if (load_rc == 1) {
        out.success = false;
        out.error_message = "Failed to load worker options";
        return out;
    }

    std::cout << "[Loom] -> Writing result to: " << out.output_uri << "\n";

    std::vector<float> final_image =
        deep_compositor::ProcessAllEXR(opts, task.height(), task.width(), images_info);
    exrio::WriteFlatOutputs(final_image, out.output_uri, opts.flat_output, opts.png_output,
                            task.width(), task.height());

    std::cout << "[Loom] -> Engine Composite execution completed.\n";
    return out;
}

std::optional<coordinator_worker::TaskOutcome> HandlePackage(const GetWorkStreamResponse& package) {
    if (!package.has_composite_task()) {
        std::cerr << "[LOOM]: Error: Received unsupported task type for task " << package.task_id()
                  << ". Failing task.\n";
        coordinator_worker::TaskOutcome out;
        out.success = false;
        out.error_message = "Unsupported task type: expected CompositeTask";
        return out;
    }

    std::cout << "[LOOM]: Starting CompositeTask: " << package.task_id() << " for frame "
              << package.frame_id() << "\n";

    try {
        return RunCompositePipeline(package.composite_task());
    } catch (const std::exception& e) {
        coordinator_worker::TaskOutcome out;
        out.success = false;
        out.error_message = e.what();
        out.output_uri = package.composite_task().output_uri();
        std::cerr << "[LOOM]: Task computation failed: " << out.error_message << "\n";
        return out;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    coordinator_worker::Options opt;
    opt.coordinator_addr = coordinator_worker::DefaultCoordinatorAddr();
    opt.log_prefix = "[LOOM]";
    opt.worker_name_tag = "loom-worker";
    opt.capabilities = {"loom"};

    coordinator_worker::RunLoop(
        opt, [](const GetWorkStreamResponse& package) { return HandlePackage(package); });
    return 0;
}
