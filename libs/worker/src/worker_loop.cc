/// \file worker_loop.cc
/// \brief Implementation of the coordinator worker gRPC loop.

#include "coordinator_worker/worker_loop.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

#include "proto/coordinator/v1/coordinator.grpc.pb.h"

using api::proto::coordinator::v1::CoordinatorService;
using api::proto::coordinator::v1::GetWorkStreamRequest;
using api::proto::coordinator::v1::ReportTaskResultRequest;
using api::proto::coordinator::v1::ReportTaskResultResponse;
using api::proto::coordinator::v1::WorkPackage;
using grpc::Channel;
using grpc::ClientContext;

namespace coordinator_worker {

std::string DefaultCoordinatorAddr() {
    if (const char* env_addr = std::getenv("COORDINATOR_ADDR")) {
        return env_addr;
    }
    return "localhost:50051";
}

std::string MakeWorkerId(std::string_view worker_name_tag) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    return std::string(worker_name_tag) + "-" +
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(dis(gen));
}

void RunLoop(const Options& opt, PackageHandler handler) {
    const std::string worker_id = MakeWorkerId(opt.worker_name_tag);
    const std::string& lp = opt.log_prefix;

    std::cout << lp << ": Starting worker loop, ID: " << worker_id << "\n";
    std::cout << lp << ": Coordinator Address: " << opt.coordinator_addr << "\n";

    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(opt.coordinator_addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<CoordinatorService::Stub> stub = CoordinatorService::NewStub(channel);

    int backoff_ms = 100;
    const int max_backoff_ms = 30000;

    while (true) {
        ClientContext context;
        GetWorkStreamRequest request;
        request.set_worker_id(worker_id);
        for (const auto& cap : opt.capabilities) {
            request.add_capabilities(cap);
        }

        std::unique_ptr<grpc::ClientReader<WorkPackage>> stream(
            stub->GetWorkStream(&context, request));

        WorkPackage package;
        while (stream->Read(&package)) {
            std::optional<TaskOutcome> outcome;
            try {
                outcome = handler(package);
            } catch (const std::exception& e) {
                std::cerr << lp << ": Handler threw exception for task " << package.task_id()
                          << ": " << e.what() << "\n";
                outcome =
                    TaskOutcome{.success = false, .error_message = e.what(), .output_uri = {}};
            } catch (...) {
                std::cerr << lp << ": Handler threw unknown exception for task "
                          << package.task_id() << "\n";
                outcome = TaskOutcome{
                    .success = false, .error_message = "unknown exception", .output_uri = {}};
            }
            if (!outcome.has_value()) {
                continue;
            }

            ClientContext report_context;
            std::chrono::system_clock::time_point deadline =
                std::chrono::system_clock::now() + std::chrono::seconds(10);
            report_context.set_deadline(deadline);

            ReportTaskResultRequest report_req;
            report_req.set_task_id(package.task_id());
            report_req.set_job_id(package.job_id());
            report_req.set_worker_id(worker_id);
            report_req.set_success(outcome->success);
            report_req.set_error_message(outcome->error_message);
            report_req.set_output_uri(outcome->output_uri);

            ReportTaskResultResponse report_res;
            grpc::Status report_status =
                stub->ReportTaskResult(&report_context, report_req, &report_res);
            if (!report_status.ok()) {
                std::cerr << lp
                          << ": Failed to report task result: " << report_status.error_message()
                          << "\n";
            } else {
                std::cout << lp << ": Task " << package.task_id()
                          << " result reported successfully.\n";
            }
        }

        grpc::Status status = stream->Finish();
        if (!status.ok()) {
            std::cerr << lp << ": Stream failed: " << status.error_message() << ". Retrying in "
                      << backoff_ms << "ms...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms *= 2;
            if (backoff_ms > max_backoff_ms) {
                backoff_ms = max_backoff_ms;
            }
        } else {
            // Coordinator closes the stream after one task for fair distribution.
            backoff_ms = 100;
        }
    }
}

}  // namespace coordinator_worker
