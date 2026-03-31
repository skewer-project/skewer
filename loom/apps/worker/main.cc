#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_info.h"
#include "proto/coordinator/v1/coordinator.grpc.pb.h"
#include "proto/coordinator/v1/coordinator.pb.h"

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

using api::proto::coordinator::v1::CompositeTask;
using api::proto::coordinator::v1::CoordinatorService;
using api::proto::coordinator::v1::GetWorkStreamRequest;
using api::proto::coordinator::v1::ReportTaskProgressRequest;
using api::proto::coordinator::v1::ReportTaskProgressResponse;
using api::proto::coordinator::v1::ReportTaskResultRequest;
using api::proto::coordinator::v1::ReportTaskResultResponse;
using api::proto::coordinator::v1::WorkPackage;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;

// Starts the loom worker loop.
void RunLoomWorker(const std::string& coordinator_addr) {
    // Generate a unique worker ID with time epoch and mersenne twister engine
    std::random_device rd;  // workers may spawn in same millisecond so epoch alone is not enough
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::string worker_id =
        "loom-worker-" +
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(dis(gen));

    std::cout << "[LOOM]: Starting worker loop, ID: " << worker_id << "\n";
    std::cout << "[LOOM]: Coordinator Address: " << coordinator_addr << "\n";

    // Open a channel to the coordinator
    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(coordinator_addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<CoordinatorService::Stub> stub = CoordinatorService::NewStub(channel);

    // Cooldown timer before acquiring stream again on retry
    int backoff_ms = 100;
    const int max_backoff_ms = 30000;  // 30 seconds max

    // Main registration loop with coordiantor
    while (true) {
        ClientContext context;
        GetWorkStreamRequest request;
        request.set_worker_id(worker_id);
        request.add_capabilities("loom");

        std::unique_ptr<ClientReader<WorkPackage>> stream(stub->GetWorkStream(&context, request));

        // Loop to read work packages from the coordinator
        WorkPackage package;
        while (stream->Read(&package)) {
            bool success = true;
            std::string error_message = "";
            std::string output_uri = "";

            try {
                if (package.has_composite_task()) {
                    const CompositeTask& task = package.composite_task();
                    output_uri = task.output_uri();
                    std::cout << "[LOOM]: Starting CompositeTask: " << package.task_id()
                              << " for frame " << package.frame_id() << "\n";

                    // Load Phase
                    std::vector<std::string> inputFiles(task.layer_uris().begin(),
                                                        task.layer_uris().end());

                    std::cout << "[Loom] -> Composite Inputs (" << inputFiles.size()
                              << " files):\n";
                    for (const auto& uri : inputFiles) {
                        std::cout << "  - " << uri << "\n";
                    }

                    std::vector<float> z_offsets(inputFiles.size() > 1 ? inputFiles.size() - 1 : 0,
                                                 0.0f);
                    Options opts = {
                        inputFiles,
                        z_offsets,
                        "",  // coordinator handles output prefixing and parsing
                    };

                    // Populate image info using opts
                    std::vector<std::unique_ptr<deep_compositor::DeepInfo>> imagesInfo;
                    int success = exrio::SaveImageInfo(opts, imagesInfo);
                    if (success == 1) {
                        std::cerr << "[Loom] Error: Failed to load worker options\n";
                        continue;
                    }

                    // Process and write image in different formats
                    std::cout << "[Loom] -> Writing result to: " << output_uri << "\n";

                    // 1) Spawn heartbeat thread before expensive compositing begins
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

                    int real_width = task.width();
                    int real_height = task.height();
                    if ((real_width == 0 || real_height == 0) && !imagesInfo.empty()) {
                        real_width = imagesInfo[0]->width();
                        real_height = imagesInfo[0]->height();
                    }

                    // Write deep EXR (handles if deep output is wanted)
                    std::vector<float> finalImage =
                        deep_compositor::ProcessAllEXR(opts, real_height, real_width, imagesInfo);
                    // Writes flat outputs if needed
                    exrio::WriteFlatOutputs(finalImage, output_uri, opts.flat_output,
                                            opts.png_output, real_width, real_height);

                    std::cout << "[Loom] -> Engine Composite execution completed.\n";

                } else {
                    std::cerr << "[LOOM]: Error: Received unsupported task type. Ignoring.\n";
                    continue;
                }
            } catch (const std::exception& e) {
                success = false;
                error_message = e.what();
                std::cerr << "[LOOM]: Task computation failed: " << error_message << "\n";
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
            report_req.set_output_uri(output_uri);

            ReportTaskResultResponse report_res;
            Status report_status = stub->ReportTaskResult(&report_context, report_req, &report_res);
            if (!report_status.ok()) {
                std::cerr << "[LOOM]: Failed to report task result: "
                          << report_status.error_message() << "\n";
            } else {
                std::cout << "[LOOM]: Task " << package.task_id()
                          << " result reported successfully.\n";
            }
        }

        // If we get here, the stream closed (either coordinator shut it down, or a network error)
        grpc::Status status = stream->Finish();
        if (!status.ok()) {
            std::cerr << "[LOOM]: Stream failed: " << status.error_message() << ". Retrying in "
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

    RunLoomWorker(coordinator_addr);
    return 0;
}
