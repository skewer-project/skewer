# Distributed Rendering Architecture: The "Pull" Model

This document outlines the architectural shift to a **Worker-Pull** model for the Skewer distributed renderer. This model simplifies network configuration, enhances fault tolerance with Spot Instances, and streamlines autoscaling on GKE.

## 1. High-Level Workflow

### A. Job Submission
1.  **User (CLI)** sends a `SubmitJob` request to the **Coordinator**.
2.  **Coordinator** decomposes the job into granular **Tasks**:
    *   *Render Tasks:* One per layer (e.g., "Background", "Character").
    *   *Composite Tasks:* One per frame (merging all layers).
3.  **Coordinator** adds these tasks to an internal `JobQueue`.
4.  **Coordinator** exposes a metric (e.g., `queue_length`) for KEDA.

### B. Autoscaling (Infrastructure)
1.  **KEDA (Kubernetes Event-Driven Autoscaling)** polls the Coordinator's metric.
2.  If `queue_length > 0`, KEDA instructs GKE to scale up the **Worker Deployment**.
3.  New **Worker Pods** (C++) boot up.

### C. Task Execution (The "Pull" Loop)
1.  **Worker** starts and enters a loop.
2.  **Worker** calls `GetTask(worker_id)` on the Coordinator.
3.  **Coordinator** pops a task from the queue and assigns it to the worker.
    *   *If queue is empty:* Coordinator returns `Wait`. Worker sleeps and retries.
4.  **Worker** processes the task:
    *   *Render Task:* Loads scene -> Renders Layer -> Writes EXR to GCS.
    *   *Composite Task:* Downloads EXR layers -> Merges -> Writes Image to GCS.
5.  **Worker** calls `UpdateTaskStatus(TaskResult)` to report success/failure.
6.  **Worker** loops back to Step 2.

### D. Job Completion
1.  When all *Render Tasks* for a frame are `COMPLETED`, the Coordinator creates a *Composite Task*.
2.  A Worker picks up the *Composite Task* and produces the final image.
3.  User polls `GetJobStatus` and receives the final GCS URI.

---

## 2. Protobuf Restructuring

We will consolidate logic into `coordinator.proto`. The `renderer.proto` and `compositor.proto` files essentially become **data definition files** rather than service definitions, as the Workers are no longer Servers.

### A. `api/proto/renderer/v1/renderer.proto`
*   **Action:** **KEEP**, but remove `service RendererService`.
*   **Purpose:** Use `RenderLayerRequest` as the data structure to describe a "Render Task".
*   **Changes:**
    *   Remove `service RendererService { ... }`.
    *   Keep `message RenderLayerRequest` (this defines *what* to render).

### B. `api/proto/compositor/v1/compositor.proto`
*   **Action:** **KEEP**, but remove `service CompositorService`.
*   **Purpose:** Use `CompositeDeepLayersRequest` as the data structure to describe a "Composite Task".
*   **Changes:**
    *   Remove `service CompositorService { ... }`.
    *   Keep `message CompositeDeepLayersRequest`.

### C. `api/proto/coordinator/v1/coordinator.proto`
*   **Action:** **EXPAND**. This becomes the single API for the entire system.
*   **New RPCs:** `GetTask`, `UpdateTaskStatus`.
*   **Updated RPCs:** `RegisterWorker` (simplified), `Heartbeat` (optional, as `GetTask` acts as a heartbeat).

#### Revised `coordinator.proto` Definition:

```protobuf
syntax = "proto3";
package api.proto.coordinator.v1;
option go_package = "./api/proto/coordinator/v1";

// Import the data structures from the other files
import "renderer/v1/renderer.proto";
import "compositor/v1/compositor.proto";

service CoordinatorService {
  // --- User-Facing API ---
  rpc SubmitJob(SubmitJobRequest) returns (SubmitJobResponse);
  rpc GetJobStatus(GetJobStatusRequest) returns (GetJobStatusResponse);

  // --- Worker-Facing API (The "Pull" Mechanism) ---
  
  // 1. Worker connects and asks for work. 
  // Blocks until work is available or returns "Wait" immediately.
  rpc GetTask(GetTaskRequest) returns (GetTaskResponse);

  // 2. Worker reports the result of a task (Success/Failure + Output URIs)
  rpc UpdateTaskStatus(UpdateTaskStatusRequest) returns (UpdateTaskStatusResponse);
  
  // 3. (Optional) Workers can still register to provide metadata (CPU count, etc.)
  rpc RegisterWorker(RegisterWorkerRequest) returns (RegisterWorkerResponse);
}

// ... SubmitJobRequest, GetJobStatusRequest remain the same ...

message GetTaskRequest {
  string worker_id = 1;
  // Capability tags? e.g. ["gpu", "high-mem"]
  repeated string tags = 2; 
}

message GetTaskResponse {
  enum TaskType {
    TASK_TYPE_WAIT = 0;      // No work, sleep and retry
    TASK_TYPE_RENDER = 1;    // Do a render
    TASK_TYPE_COMPOSITE = 2; // Do a composite
  }
  TaskType task_type = 1;
  string task_id = 2;

  // Only one of these will be set, depending on task_type
  api.proto.renderer.v1.RenderLayerRequest render_task = 3;
  api.proto.compositor.v1.CompositeDeepLayersRequest composite_task = 4;
}

message UpdateTaskStatusRequest {
  string task_id = 1;
  string worker_id = 2;
  bool success = 3;
  string error_message = 4;

  // If success, where is the output?
  string output_uri = 5;
  
  // Performance metrics
  int64 execution_time_ms = 6;
}

message UpdateTaskStatusResponse {
  // Acknowledgment
  bool success = 1; 
}
```

## 3. Implementation Plan

### Phase 1: Proto Updates & Go Skeleton
1.  **Modify Protos:** Apply the changes above. Regenerate Go and C++ bindings.
2.  **Update `main.go`:** Ensure it only registers `CoordinatorService`.
3.  **Update `scheduler.go`:** Implement the `TaskQueue` (just a `slice` or `channel` for now).

### Phase 2: C++ Worker Loop
1.  **Refactor Worker:**
    *   Remove `grpc::Server`.
    *   Add `grpc::Client` connecting to Coordinator.
    *   Implement `while(true)` loop:
        *   `GetTask()`
        *   `if (RENDER)` -> call existing Render logic.
        *   `UpdateTaskStatus()`

### Phase 3: Cloud & KEDA
1.  **Dockerize:** Build `skewer-worker` image.
2.  **Deploy:** Helm chart with `Deployment` (replicas: 0).
3.  **Autoscale:** Add KEDA `ScaledObject` pointing to Coordinator's metric endpoint (e.g., `/metrics` if using Prometheus, or a custom API).
