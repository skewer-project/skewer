#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "proto/coordinator/v1/coordinator.grpc.pb.h"
#include "proto/coordinator/v1/coordinator.pb.h"
#include "session/render_options.h"
#include "session/render_session.h"

using api::proto::coordinator::v1::CoordinatorService;
using api::proto::coordinator::v1::GetWorkStreamRequest;
using api::proto::coordinator::v1::RenderTask;
using api::proto::coordinator::v1::ReportTaskResultRequest;
using api::proto::coordinator::v1::ReportTaskResultResponse;
using api::proto::coordinator::v1::WorkPackage;
using grpc::Channel;
using grpc::ClientContext;

void RunSkewerWorker(const std::string& coordinator_addr) {
    // Generate a unique worker ID with epoch (may change in the future)
    std::string worker_id =
        "skewer-worker-" +
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    // Determine number of render threads from environment
    int render_threads = 2;  // default fallback
    if (const char* env_threads = std::getenv("RENDER_THREADS")) {
        try {
            render_threads = std::stoi(env_threads);
        } catch (...) {
            std::cerr << "[SKEWER]: Error parsing RENDER_THREADS. Using default 2.\n";
        }
    }

    std::cout << "[SKEWER]: Starting worker loop, ID: " << worker_id << "\n";
    std::cout << "[SKEWER]: Coordinator Address: " << coordinator_addr << "\n";
    std::cout << "[SKEWER]: Render Threads: " << render_threads << "\n";

    // Open a gRPC channel to the coordinator
    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(coordinator_addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<CoordinatorService::Stub> stub = CoordinatorService::NewStub(channel);

    // Main GetWorkStream loop
    while (true) {
        ClientContext context;
        GetWorkStreamRequest request;
        request.set_worker_id(worker_id);
        request.add_capabilities("skewer");  // may add more capabilities later

        // Actually get the stream work package
        std::unique_ptr<grpc::ClientReader<WorkPackage>> stream(
            stub->GetWorkStream(&context, request));

        WorkPackage package;
        while (stream->Read(&package)) {
            if (!package.has_render_task()) {
                std::cerr << "[SKEWER]: Error: Received non-render task. Ignoring.\n";
                continue;
            }

            // Extract the render task from the work package
            const RenderTask& task = package.render_task();
            std::cout << "[SKEWER]: Starting RenderTask: " << package.task_id() << " for frame "
                      << package.frame_id() << "\n";
            std::cout << "[SKEWER]: Output URI: " << task.output_uri() << "\n";

            bool success = true;
            std::string error_message = "";

            try {
                // Initialize engine and RENDER HERE
                skwr::RenderSession session;

                // Determine thread count for this session
                int task_threads = render_threads;
                if (task.threads() > 0) {
                    task_threads = task.threads();
                }

                // Adapt integrator config to sample range
                session.LoadSceneFromFile(task.scene_uri(), 0);
                session.Options().image_config.width = task.width();
                session.Options().image_config.height = task.height();
                session.Options().integrator_config.start_sample = task.sample_start();
                session.Options().integrator_config.max_samples =
                    task.sample_end() - task.sample_start();
                session.Options().integrator_config.num_threads = task_threads;
                session.Options().image_config.outfile = task.output_uri();
                session.Options().integrator_config.enable_deep = task.enable_deep();

                std::cout << "[SKEWER]: Rendering " << task.width() << "x" << task.height()
                          << " (Samples: " << task.sample_start() << " to " << task.sample_end()
                          << " | Threads: " << task_threads << ")\n";

                // Re-initialize the film with the updated (smaller) chunk sample count
                // to prevent huge memory allocations for deep renders.
                session.RebuildFilm();

                // Now render the task
                session.Render();
                session.Save();

                // std::cout << "[SKEWER]: Engine execution placeholder completed.\n";

            } catch (const std::exception& e) {
                success = false;
                error_message = e.what();
                std::cerr << "[SKEWER]: Task computation failed: " << error_message << "\n";
            }

            // Report the result
            ClientContext report_context;
            ReportTaskResultRequest report_req;
            report_req.set_task_id(package.task_id());
            report_req.set_job_id(package.job_id());
            report_req.set_worker_id(worker_id);
            report_req.set_success(success);
            report_req.set_error_message(error_message);
            report_req.set_output_uri(task.output_uri());

            ReportTaskResultResponse report_res;
            ::grpc::Status report_status =
                stub->ReportTaskResult(&report_context, report_req, &report_res);
            if (!report_status.ok()) {
                std::cerr << "[SKEWER]: Failed to report task result: "
                          << report_status.error_message() << "\n";
            } else {
                std::cout << "[SKEWER]: Task " << package.task_id()
                          << " result reported successfully.\n";
            }
        }

        // After stream closes, we loop back and try to get a new stream immediately.
        // We only sleep if it was a real error or if there's no work.
        // The Coordinator now closes the stream after 1 task to ensure fair distribution.
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // Determine coordinator address
    std::string coordinator_addr = "localhost:50051";
    if (const char* env_addr = std::getenv("COORDINATOR_ADDR")) {
        coordinator_addr = env_addr;
    }

    RunSkewerWorker(coordinator_addr);
    return 0;
}
