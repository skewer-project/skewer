# gRPC API Reference

This document describes the internal gRPC `CoordinatorService` used by the Skewer rendering pipeline. External clients interact with the [HTTP API](http.md) instead — the HTTP API proxies to this gRPC service after enforcing authentication, rate limiting, ownership, and scene normalization.

## Service Definition

Located in: `api/proto/coordinator/v1/coordinator.proto`

```protobuf
package api.proto.coordinator.v1;

option go_package = "github.com/skewer-project/skewer/api/proto/coordinator/v1;coordinatorv1";
```

## CoordinatorService

Workers communicate directly with Cloud Batch/Workflows, not this service.

### SubmitPipeline

Submits a scene-first rendering request. The coordinator downloads `scene_uri`, parses it, classifies layers as static or animated, derives `num_frames` from the animation block, and orchestrates rendering.

```protobuf
rpc SubmitPipeline(SubmitPipelineRequest) returns (SubmitPipelineResponse);
```

**Request:**
```protobuf
message SubmitPipelineRequest {
    string pipeline_id = 1;                 // Optional; server generates a UUID if empty
    string scene_uri = 2;                   // GCS path to root scene.json (e.g. gs://bucket/scenes/scene.json)
    string composite_output_uri_prefix = 3; // GCS prefix for final composited frames
    bool enable_cache = 4;                  // Enable content-hash layer caching (default false)
}
```

**Response:**
```protobuf
message SubmitPipelineResponse {
    string pipeline_id = 1;
}
```

### GetPipelineStatus

Retrieves the current status of a submitted pipeline.

```protobuf
rpc GetPipelineStatus(GetPipelineStatusRequest) returns (GetPipelineStatusResponse);
```

**Request:**
```protobuf
message GetPipelineStatusRequest {
    string pipeline_id = 1;
}
```

**Response:**
```protobuf
message GetPipelineStatusResponse {
    enum PipelineStatus {
        PIPELINE_STATUS_UNSPECIFIED = 0;
        PIPELINE_STATUS_RUNNING = 1;
        PIPELINE_STATUS_SUCCEEDED = 2;
        PIPELINE_STATUS_FAILED = 3;
        PIPELINE_STATUS_CANCELLED = 4;
    }
    PipelineStatus status = 1;
    string error_message = 2;
    map<string, string> layer_outputs = 3; // layer_id -> GCS output prefix
    string composite_output = 4;           // GCS prefix of final composited frames
}
```

### CancelPipeline

Cancels a running pipeline.

```protobuf
rpc CancelPipeline(CancelPipelineRequest) returns (CancelPipelineResponse);
```

**Request:**
```protobuf
message CancelPipelineRequest {
    string pipeline_id = 1;
}
```

**Response:**
```protobuf
message CancelPipelineResponse {
    bool success = 1;
}
```

## Code Generation

Generate Go code:

```bash
bash scripts/gen_proto.sh
```

This generates:
- `api/proto/coordinator/v1/coordinator.pb.go` — Protobuf messages
- `api/proto/coordinator/v1/coordinator_grpc.pb.go` — gRPC service stubs
