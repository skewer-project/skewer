# gRPC API Reference

This document describes the internal gRPC `CoordinatorService` used by the Skewer rendering pipeline. External clients interact with the [HTTP API](http.md) instead — the HTTP API proxies to this gRPC service after enforcing authentication, rate limiting, ownership, and scene normalization.

## Service Definition

Located in: `api/proto/coordinator/v1/coordinator.proto`

```protobuf
package api.proto.coordinator.v1;

service CoordinatorService {
    // SubmitJob validates and submits a rendering job
    rpc SubmitJob(SubmitJobRequest) returns (SubmitJobResponse);

    // GetJobStatus returns the current status of a submitted job
    rpc GetJobStatus(GetJobStatusRequest) returns (GetJobStatusResponse);

    // CancelJob attempts to cancel a running job
    rpc CancelJob(CancelJobRequest) returns (CancelJobResponse);
}
```

## Messages

### SubmitJobRequest

```protobuf
message SubmitJobRequest {
    string scene_uri = 1;
    int32 num_frames = 2;
    RenderSettings overrides = 3;
}
```

| Field | Type | Description |
|-------|------|-------------|
| `scene_uri` | `string` | URI of the scene manifest (`gs://` or local path) |
| `num_frames` | `int32` | Number of frames to render |
| `overrides` | `RenderSettings` | Optional overrides for render settings |

### SubmitJobResponse

```protobuf
message SubmitJobResponse {
    string job_id = 1;
    string workflow_name = 2;
    JobStatus status = 3;
}
```

### GetJobStatusRequest

```protobuf
message GetJobStatusRequest {
    string job_id = 1;
}
```

### GetJobStatusResponse

```protobuf
message GetJobStatusResponse {
    string job_id = 1;
    JobStatus status = 2;
    float progress = 3;
    string error_message = 4;
}
```

### CancelJobRequest

```protobuf
message CancelJobRequest {
    string job_id = 1;
}
```

### CancelJobResponse

```protobuf
message CancelJobResponse {
    bool success = 1;
    string message = 2;
}
```

### RenderSettings

```protobuf
message RenderSettings {
    int32 samples_per_pixel = 1;
    int32 width = 2;
    int32 height = 3;
    bool enable_deep = 4;
}
```

### JobStatus

```protobuf
enum JobStatus {
    JOB_STATUS_UNSPECIFIED = 0;
    JOB_STATUS_PENDING = 1;
    JOB_STATUS_RUNNING = 2;
    JOB_STATUS_COMPLETED = 3;
    JOB_STATUS_FAILED = 4;
    JOB_STATUS_CANCELLED = 5;
}
```

## See Also

- [HTTP API](http.md) - External API layer
- [Coordinator Architecture](coordinator.md) - Service internals and deployment
