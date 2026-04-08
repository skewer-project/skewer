/// \file worker_loop.h
/// \brief Shared gRPC client loop for Skewer and Loom worker processes.
///
/// Connects to `CoordinatorService`, streams `GetWorkStreamResponse` messages, dispatches to
/// engine-specific handlers, and reports results. Intended for use with
/// `clang-doc` and other Clang-based documentation tools.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "proto/coordinator/v1/coordinator.pb.h"

namespace coordinator_worker {

/// \brief Configuration for \ref RunLoop.
struct Options {
    /// gRPC endpoint (host:port) of the coordinator service.
    std::string coordinator_addr;
    /// Prefix for log lines (no trailing colon); e.g. `"[SKEWER]"`, `"[LOOM]"`.
    std::string log_prefix;
    /// Stable name embedded in the generated worker id (no trailing dash); e.g.
    /// `"skewer-worker"` produces ids like `skewer-worker-<epoch>-<rand>`.
    std::string worker_name_tag;
    /// Capability tags sent on `GetWorkStream`; must match tasks the worker can run.
    std::vector<std::string> capabilities;
};

/// \brief Result of handling one work unit, reported via `ReportTaskResult`.
struct TaskOutcome {
    /// Whether task execution completed without a terminal error.
    bool success = true;
    /// Populated when `success` is false; may be shown in job status.
    std::string error_message;
    /// Primary output location (rendered frame, composite image, etc.).
    std::string output_uri;
};

/// \brief Invoked for each streamed work unit.
///
/// \returns `std::nullopt` if the package was ignored (no `ReportTaskResult` call).
/// Otherwise returns the outcome to report (including `success == false` failures).
using PackageHandler = std::function<std::optional<TaskOutcome>(
    const api::proto::coordinator::v1::GetWorkStreamResponse&)>;

/// \brief Runs until process exit: connect, stream work, run `handler`, report results.
///
/// Opens an insecure gRPC channel, registers with `GetWorkStream` using `options`, and
/// loops forever. On stream errors, sleeps with exponential backoff (capped at 30s)
/// and reconnects. When the coordinator closes the stream cleanly after a task, backoff
/// resets and a new connection is opened immediately.
///
/// \param options Connection identity, logging prefix, coordinator address, capabilities.
/// \param handler Called per package; see \ref PackageHandler for `std::nullopt` semantics.
void RunLoop(const Options& options, PackageHandler handler);

/// \brief Coordinator gRPC target from the environment, with a localhost default.
///
/// \returns `COORDINATOR_ADDR` if set, otherwise `"localhost:50051"`.
std::string DefaultCoordinatorAddr();

/// \brief Builds a globally unique worker id for coordinator bookkeeping.
///
/// \param worker_name_tag See `Options::worker_name_tag`.
/// \returns Concatenation of `worker_name_tag`, an epoch-based timestamp, and a random suffix.
std::string MakeWorkerId(std::string_view worker_name_tag);

}  // namespace coordinator_worker
