/// \file worker_loop.cc
/// \brief gRPC client helpers for coordinator workers.

#include "coordinator_worker/worker_loop.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>

#include "proto/coordinator/v1/coordinator.grpc.pb.h"

using api::proto::coordinator::v1::CoordinatorService;
using api::proto::coordinator::v1::ReportTaskResultRequest;
using api::proto::coordinator::v1::ReportTaskResultResponse;
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

bool ReportTaskResult(const std::string& coordinator_addr, const std::string& worker_id,
                      const std::string& task_id, const std::string& job_id,
                      const TaskOutcome& outcome, int64_t execution_time_ms) {
    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(coordinator_addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<CoordinatorService::Stub> stub = CoordinatorService::NewStub(channel);

    ClientContext context;
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(60);
    context.set_deadline(deadline);

    ReportTaskResultRequest req;
    req.set_task_id(task_id);
    req.set_job_id(job_id);
    req.set_worker_id(worker_id);
    req.set_success(outcome.success);
    req.set_error_message(outcome.error_message);
    req.set_output_uri(outcome.output_uri);
    req.set_execution_time_ms(execution_time_ms);

    ReportTaskResultResponse res;
    grpc::Status st = stub->ReportTaskResult(&context, req, &res);
    if (!st.ok()) {
        std::cerr << "ReportTaskResult failed: " << st.error_message() << "\n";
        return false;
    }
    return res.acknowledged();
}

}  // namespace coordinator_worker
