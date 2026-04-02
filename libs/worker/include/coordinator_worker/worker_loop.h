/// \file worker_loop.h
/// \brief gRPC helpers for Skewer and Loom worker processes (ReportTaskResult).

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coordinator_worker {

/// \brief Result of one task, reported via `ReportTaskResult`.
struct TaskOutcome {
    bool success = true;
    std::string error_message;
    std::string output_uri;
};

/// \brief Coordinator gRPC target from the environment, with a localhost default.
///
/// \returns `COORDINATOR_ADDR` if set, otherwise `"localhost:50051"`.
std::string DefaultCoordinatorAddr();

/// \brief Builds a globally unique worker id for coordinator bookkeeping.
std::string MakeWorkerId(std::string_view worker_name_tag);

/// \brief Sends a single `ReportTaskResult` RPC (non-streaming).
bool ReportTaskResult(const std::string& coordinator_addr, const std::string& worker_id,
                      const std::string& task_id, const std::string& job_id,
                      const TaskOutcome& outcome, int64_t execution_time_ms = 0);

}  // namespace coordinator_worker
