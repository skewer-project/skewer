#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "exrio/deep_image.h"
#include "proto/coordinator/v1/coordinator.grpc.pb.h"
#include "proto/coordinator/v1/coordinator.pb.h"

// TODO: Include necessary Loom engine headers
// #include <loom/engine/loom_session.h>

using api::proto::coordinator::v1::CompositeTask;
using api::proto::coordinator::v1::CoordinatorService;
using api::proto::coordinator::v1::GetWorkStreamRequest;
using api::proto::coordinator::v1::MergeTask;
using api::proto::coordinator::v1::ReportTaskResultRequest;
using api::proto::coordinator::v1::ReportTaskResultResponse;
using api::proto::coordinator::v1::WorkPackage;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;

// Starts the loom worker loop.
void RunLoomWorker(const std::string& coordinator_addr) {
    std::string worker_id =
        "loom-worker-" + std::to_string(std::chrono::system_clock::now()
                                            .time_since_epoch()
                                            .count());  // epoch time is fine for now

    std::cout << "[Loom] Starting worker loop, ID: " << worker_id << "\n";
    std::cout << "[Loom] Coordinator Address: " << coordinator_addr << "\n";

    // Open a channel to the coordinator
    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(coordinator_addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<CoordinatorService::Stub> stub = CoordinatorService::NewStub(channel);

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

            // NOTE: FUNCTIONALLY, a MergeTask is the same as a CompositeTask but they serve
            // different purposes semantically
            try {
                if (package.has_merge_task()) {
                    const MergeTask& task = package.merge_task();
                    output_uri = task.output_uri();
                    std::cout << "[Loom] Starting MergeTask: " << package.task_id() << " for frame "
                              << package.frame_id() << "\n";

                    // TODO: Pass partial deep EXRs to loom engine and merge
                    // Load Phase
                    std::vector<std::string> inputPartialFiles(task.partial_deep_exr_uris().begin(),
                                                               task.partial_deep_exr_uris().end());

                    std::cout << "[Loom] -> Merge Inputs (" << inputPartialFiles.size()
                              << " files):\n";
                    for (const auto& uri : inputPartialFiles) {
                        std::cout << "  - " << uri << "\n";
                    }

                    std::vector<exrio::DeepImage> images =
                        exrio::LoadImagesPhase(inputPartialFiles);

                    // Merge Phase
                    float taskMergeThreshold =
                        0.001F;  // TODO: include mergeThreshold in MergeTask protobuf
                    exrio::CompositorOptions compOpts;
                    compOpts.mergeThreshold = taskMergeThreshold;
                    compOpts.enableMerging = (taskMergeThreshold > 0.0f);

                    exrio::CompositorStats stats;
                    exrio::DeepImage merged = deepMerge(images, compOpts, &stats);

                    // Flatten Phase (should be more configurable)
                    bool isDeepOutput = output_uri.ends_with(
                        ".exr");  // TODO: should check if user specifically wants deep vs flat
                    bool isFlatOutput = false;
                    bool isPngOutput = output_uri.ends_with(".png");
                    std::vector<float> flatRgba;

                    if (isPngOutput || isFlatOutput) {
                        flatRgba = exrio::FlattenPhase(merged);
                    }

                    // Write Phase
                    std::cout << "[Loom] -> Writing result to: " << output_uri << "\n";
                    exrio::WriteOutputsPhase(merged, flatRgba, output_uri, isDeepOutput,
                                             isFlatOutput, isPngOutput);

                    // TODO: Consider logging to kubectl logs

                    std::cout << "[Loom] -> Engine Merge execution completed.\n";

                } else if (package.has_composite_task()) {
                    const CompositeTask& task = package.composite_task();
                    output_uri = task.output_uri();
                    std::cout << "[Loom] Starting CompositeTask: " << package.task_id()
                              << " for frame " << package.frame_id() << "\n";

                    // TODO: Pass partial deep EXRs to loom engine and merge
                    // Load Phase
                    std::vector<std::string> inputPartialFiles(task.layer_uris().begin(),
                                                               task.layer_uris().end());

                    std::cout << "[Loom] -> Composite Inputs (" << inputPartialFiles.size()
                              << " files):\n";
                    for (const auto& uri : inputPartialFiles) {
                        std::cout << "  - " << uri << "\n";
                    }

                    std::vector<exrio::DeepImage> images =
                        exrio::LoadImagesPhase(inputPartialFiles);

                    // Merge Phase
                    float taskMergeThreshold =
                        0.001F;  // TODO: include mergeThreshold in MergeTask protobuf
                    exrio::CompositorOptions compOpts;
                    compOpts.mergeThreshold = taskMergeThreshold;
                    compOpts.enableMerging = (taskMergeThreshold > 0.0f);

                    exrio::CompositorStats stats;
                    exrio::DeepImage merged = deepMerge(images, compOpts, &stats);

                    // Flatten Phase (should be more configurable)
                    bool isDeepOutput = output_uri.ends_with(
                        ".exr");  // TODO: should check if user specifically wants deep vs flat
                    bool isFlatOutput = false;
                    bool isPngOutput = output_uri.ends_with(".png");
                    std::vector<float> flatRgba;

                    if (isPngOutput || isFlatOutput) {
                        flatRgba = exrio::FlattenPhase(merged);
                    }

                    // Write Phase
                    std::cout << "[Loom] -> Writing result to: " << output_uri << "\n";
                    exrio::WriteOutputsPhase(merged, flatRgba, output_uri, isDeepOutput,
                                             isFlatOutput, isPngOutput);

                    // TODO: Consider logging to kubectl logs
                    std::cout << "[Loom] -> Engine Composite execution completed.\n";

                } else {
                    std::cerr << "[Loom] Error: Received unsupported task type. Ignoring.\n";
                    continue;
                }
            } catch (const std::exception& e) {
                success = false;
                error_message = e.what();
                std::cerr << "[Loom] Task computation failed: " << error_message << "\n";
            }

            // Report the result
            ClientContext report_context;
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
                std::cerr << "[Loom] Failed to report task result: "
                          << report_status.error_message() << "\n";
            } else {
                std::cout << "[Loom] Task " << package.task_id()
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

    RunLoomWorker(coordinator_addr);
    return 0;
}
