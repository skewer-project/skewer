#include <coordinator_worker/worker_loop.h>
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>

#include "proto/coordinator/v1/coordinator.grpc.pb.h"
#include "proto/coordinator/v1/coordinator.pb.h"
#include "session/render_options.h"
#include "session/render_session.h"

struct HeartbeatGuard {
    std::atomic<bool>& active;
    std::thread& th;
    ~HeartbeatGuard() {
        active.store(false);
        if (th.joinable()) {
            th.join();
        }
    }
};

using api::proto::coordinator::v1::CoordinatorService;
using api::proto::coordinator::v1::GetWorkStreamRequest;
using api::proto::coordinator::v1::GetWorkStreamResponse;
using api::proto::coordinator::v1::RenderTask;
using api::proto::coordinator::v1::ReportTaskProgressRequest;
using api::proto::coordinator::v1::ReportTaskProgressResponse;
using api::proto::coordinator::v1::ReportTaskResultRequest;
using api::proto::coordinator::v1::ReportTaskResultResponse;
using grpc::Channel;
using grpc::ClientContext;

void RunSkewerWorker(const std::string& coordinator_addr) {
    // Generate a unique worker ID with time epoch and mersenne twister engine
    std::random_device rd;  // workers may spawn in same millisecond so epoch alone is not enough
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::string worker_id =
        "skewer-worker-" +
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(dis(gen));

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

    // Cooldown timer before acquiring stream again on retry
    int backoff_ms = 100;
    const int max_backoff_ms = 30000;  // 30 seconds max

    // Main GetWorkStream loop
    while (true) {
        ClientContext context;
        GetWorkStreamRequest request;
        request.set_worker_id(worker_id);
        request.add_capabilities("skewer");  // may add more capabilities later

        // Actually get the stream work package
        std::unique_ptr<grpc::ClientReader<GetWorkStreamResponse>> stream(
            stub->GetWorkStream(&context, request));

        GetWorkStreamResponse package;
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

                // Check for overrides from the coordinator
                if (task.width() > 0 && task.height() > 0) {
                    session.Options().image_config.width = task.width();
                    session.Options().image_config.height = task.height();
                }
                if (task.max_samples() > 0) {
                    session.Options().integrator_config.max_samples = task.max_samples();
                    std::cout << "[SKEWER]: Overriding JSON samples with: " << task.max_samples()
                              << "\n";
                }

                session.Options().integrator_config.num_threads = task_threads;
                session.Options().image_config.outfile = task.output_uri();
                session.Options().image_config.exrfile = task.output_uri();
                session.Options().integrator_config.enable_deep = task.enable_deep();

                // Apply adaptive sampling if the job specifies it (overrides scene JSON)
                if (task.noise_threshold() > 0.0f) {
                    session.Options().integrator_config.noise_threshold = task.noise_threshold();
                }
                if (task.min_samples() > 0) {
                    session.Options().integrator_config.min_samples = task.min_samples();
                }
                if (task.adaptive_step() > 0) {
                    session.Options().integrator_config.adaptive_step = task.adaptive_step();
                }

                std::cout << "[SKEWER]: Rendering " << session.Options().image_config.width << "x"
                          << session.Options().image_config.height << " (Threads: " << task_threads
                          << ")\n";

                // 1) Spawn heartbeat thread before rendering begins
                std::atomic<bool> heartbeat_active(true);
                std::thread heartbeat_thread([&]() {
                    while (heartbeat_active.load()) {
                        for (int i = 0; i < 30;
                             ++i) {  // Sleep for 30s but check exit condition every second
                            if (!heartbeat_active.load()) return;
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        }

                        ClientContext hb_context;
                        ReportTaskProgressRequest hb_req;
                        hb_req.set_task_id(package.task_id());
                        hb_req.set_worker_id(worker_id);
                        hb_req.set_progress_percent(
                            0.0);  // Polling real progress can go here later

                        ReportTaskProgressResponse hb_res;
                        stub->ReportTaskProgress(&hb_context, hb_req, &hb_res);
                    }
                });
                HeartbeatGuard hb_guard{heartbeat_active, heartbeat_thread};

                // Now render the task (blocking)
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

            // Fail fast if the coordinator doesn't acknowledge within 10 seconds
            std::chrono::system_clock::time_point deadline =
                std::chrono::system_clock::now() + std::chrono::seconds(10);
            report_context.set_deadline(deadline);

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

        // If we get here, the stream closed (either coordinator shut it down, or a network error)
        grpc::Status status = stream->Finish();
        if (!status.ok()) {
            std::cerr << "[SKEWER]: Stream failed: " << status.error_message() << ". Retrying in "
                      << backoff_ms << "ms...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));

            backoff_ms *= 2;
            if (backoff_ms > max_backoff_ms) {
                backoff_ms = max_backoff_ms;
            }

        } else {
            // Stream closed normally (Coordinator intentionally hung up after assigning a task)
            // Reset backoff and immediately reconnect to ask for more work.
            backoff_ms = 100;
        }
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
