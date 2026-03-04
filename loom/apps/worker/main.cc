#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "proto/coordinator/v1/coordinator.grpc.pb.h"

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

void RunLoomWorker(const std::string& coordinator_addr) {
    std::string worker_id =
        "loom-worker-" +
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    std::cout << "[Loom] Starting worker loop, ID: " << worker_id << "\n";
    std::cout << "[Loom] Coordinator Address: " << coordinator_addr << "\n";

    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(coordinator_addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<CoordinatorService::Stub> stub = CoordinatorService::NewStub(channel);

    while (true) {
        ClientContext context;
        GetWorkStreamRequest request;
        request.set_worker_id(worker_id);
        request.add_capabilities("loom");

        std::unique_ptr<ClientReader<WorkPackage>> stream(stub->GetWorkStream(&context, request));

        WorkPackage package;
        while (stream->Read(&package)) {
            bool success = true;
            std::string error_message = "";
            std::string output_uri = "";

            try {
                if (package.has_merge_task()) {
                    const MergeTask& task = package.merge_task();
                    output_uri = task.output_uri();
                    std::cout << "[Loom] Starting MergeTask: " << package.task_id() << " for frame "
                              << package.frame_id() << "\n";

                    // TODO: Pass partial deep EXRs to loom engine and merge
                    std::cout << "[Loom] -> Engine Merge execution placeholder completed.\n";

                } else if (package.has_composite_task()) {
                    const CompositeTask& task = package.composite_task();
                    output_uri = task.output_uri();
                    std::cout << "[Loom] Starting CompositeTask: " << package.task_id()
                              << " for frame " << package.frame_id() << "\n";

                    // TODO: Pass layer EXRs to loom engine and composite
                    std::cout << "[Loom] -> Engine Composite execution placeholder completed.\n";

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
            Status status = stub->ReportTaskResult(&report_context, report_req, &report_res);
            if (!status.ok()) {
                std::cerr << "[Loom] Failed to report task result: " << status.error_message()
                          << "\n";
            } else {
                std::cout << "[Loom] Task " << package.task_id()
                          << " result reported successfully.\n";
            }
        }

        std::cerr << "[Loom] Stream disconnected. Reconnecting in 5 seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
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
