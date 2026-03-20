#include <coordinator_worker/worker_loop.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "proto/coordinator/v1/coordinator.pb.h"
#include "session/render_options.h"
#include "session/render_session.h"

using api::proto::coordinator::v1::RenderTask;
using api::proto::coordinator::v1::WorkPackage;

namespace {

std::optional<coordinator_worker::TaskOutcome> HandlePackage(const WorkPackage& package,
                                                             int render_threads) {
    if (!package.has_render_task()) {
        std::cerr << "[SKEWER]: Error: Received non-render task. Ignoring.\n";
        return std::nullopt;
    }

    const RenderTask& task = package.render_task();
    std::cout << "[SKEWER]: Starting RenderTask: " << package.task_id() << " for frame "
              << package.frame_id() << "\n";
    std::cout << "[SKEWER]: Output URI: " << task.output_uri() << "\n";

    coordinator_worker::TaskOutcome out;
    out.output_uri = task.output_uri();

    try {
        skwr::RenderSession session;

        int task_threads = render_threads;
        if (task.threads() > 0) {
            task_threads = task.threads();
        }

        session.LoadSceneFromFile(task.scene_uri(), 0);

        if (task.width() > 0 && task.height() > 0) {
            session.Options().image_config.width = task.width();
            session.Options().image_config.height = task.height();
        }
        if (task.max_samples() > 0) {
            session.Options().integrator_config.max_samples = task.max_samples();
            std::cout << "[SKEWER]: Overriding JSON samples with: " << task.max_samples() << "\n";
        }

        session.Options().integrator_config.num_threads = task_threads;
        session.Options().image_config.outfile = task.output_uri();
        session.Options().image_config.exrfile = task.output_uri();
        session.Options().integrator_config.enable_deep = task.enable_deep();

        if (task.noise_threshold() > 0.0f) {
            session.Options().integrator_config.noise_threshold = task.noise_threshold();
        }
        if (task.min_samples() > 0) {
            session.Options().integrator_config.min_samples = task.min_samples();
        }
        if (task.adaptive_step() > 0) {
            session.Options().integrator_config.adaptive_step = task.adaptive_step();
        }

        std::cout << "[SKEWER]: Rendering " << task.width() << "x" << task.height()
                  << " (Threads: " << task_threads << ")\n";

        session.Render();
        session.Save();

        out.success = true;
    } catch (const std::exception& e) {
        out.success = false;
        out.error_message = e.what();
        std::cerr << "[SKEWER]: Task computation failed: " << out.error_message << "\n";
    }

    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    int render_threads = 2;
    if (const char* env_threads = std::getenv("RENDER_THREADS")) {
        try {
            render_threads = std::stoi(env_threads);
        } catch (...) {
            std::cerr << "[SKEWER]: Error parsing RENDER_THREADS. Using default 2.\n";
        }
    }

    coordinator_worker::Options opt;
    opt.coordinator_addr = coordinator_worker::DefaultCoordinatorAddr();
    opt.log_prefix = "[SKEWER]";
    opt.worker_name_tag = "skewer-worker";
    opt.capabilities = {"skewer"};

    std::cout << opt.log_prefix << ": Render Threads: " << render_threads << "\n";

    coordinator_worker::RunLoop(opt, [render_threads](const WorkPackage& package) {
        return HandlePackage(package, render_threads);
    });

    return 0;
}
