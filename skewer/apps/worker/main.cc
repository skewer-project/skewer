#include <grpcpp/grpcpp.h>
#include "session/render_session.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "proto/coordinator/v1/coordinator.grpc.pb.h"

using api::proto::coordinator::v1::CoordinatorService;
using api::proto::coordinator::v1::GetWorkStreamRequest;
using api::proto::coordinator::v1::RenderTask;
using api::proto::coordinator::v1::ReportTaskResultRequest;
using api::proto::coordinator::v1::ReportTaskResultResponse;
using api::proto::coordinator::v1::WorkPackage;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;

void RunSkewerWorker(const std::string& coordinator_addr) {
    std::string worker_id =
        "skewer-worker-" +
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    std::cout << "[SKEWER]: Starting worker loop, ID: " << worker_id << "\n";
    std::cout << "[SKEWER]: Coordinator Address: " << coordinator_addr << "\n";

    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(coordinator_addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<CoordinatorService::Stub> stub = CoordinatorService::NewStub(channel);

    while (true) {
        ClientContext context;
        GetWorkStreamRequest request;
        request.set_worker_id(worker_id);
        request.add_capabilities("skewer");

        std::unique_ptr<ClientReader<WorkPackage>> stream(stub->GetWorkStream(&context, request));

        WorkPackage package;
        while (stream->Read(&package)) {
            if (!package.has_render_task()) {
                std::cerr << "[SKEWER]: Error: Received non-render task. Ignoring.\n";
                continue;
            }

            const RenderTask& task = package.render_task();
            std::cout << "[SKEWER]: Starting RenderTask: " << package.task_id() << " for frame "
                      << package.frame_id() << "\n";
            std::cout << "[SKEWER]: Output URI: " << task.output_uri() << "\n";

            bool success = true;
            std::string error_message = "";

            try {
                // Initialize engine and render
                skwr::RenderSession session;

                // Note: The actual implementation will need to fetch the scene and adapt
                // RenderSession session.LoadSceneFromFile(task.scene_uri(), 0);
                // session.RenderChunk(task.sample_start(), task.sample_end());
                // session.Save(task.output_uri());

                std::cout << "[SKEWER]: Engine execution placeholder completed.\n";

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
            Status status = stub->ReportTaskResult(&report_context, report_req, &report_res);
            if (!status.ok()) {
                std::cerr << "[SKEWER]: Failed to report task result: " << status.error_message()
                          << "\n";
            } else {
                std::cout << "[SKEWER]: Task " << package.task_id()
                          << " result reported successfully.\n";
            }
        }

        std::cerr << "[SKEWER]: Stream disconnected. Reconnecting in 5 seconds...\n";
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

    RunSkewerWorker(coordinator_addr);
    return 0;
}
