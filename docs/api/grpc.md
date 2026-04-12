# gRPC API Reference

This document describes the gRPC API between the Coordinator and Workers.

## Service Definition

Located in: `api/proto/coordinator/v1/coordinator.proto`

## CoordinatorService

### External API (CLI → Coordinator)

#### SubmitJob

```protobuf
rpc SubmitJob(SubmitJobRequest) returns (SubmitJobResponse);
```

**Request:**
```protobuf
message SubmitJobRequest {
  string job_id = 1;      // UUID
  string job_name = 2;    // e.g., "smoke_layer"
  repeated string depends_on = 3;
  int32 num_frames = 4;
  int32 width = 5;
  int32 height = 6;
  oneof job_type {
    RenderJob render_job = 7;
    CompositeJob composite_job = 8;
  }
}
```

**Response:**
```protobuf
message SubmitJobResponse {
  string job_id = 1;
}
```

#### GetJobStatus

```protobuf
rpc GetJobStatus(GetJobStatusRequest) returns (GetJobStatusResponse);
```

**Request:**
```protobuf
message GetJobStatusRequest {
  string job_id = 1;
}
```

**Response:**
```protobuf
message GetJobStatusResponse {
  enum JobStatus {
    JOB_STATUS_UNSPECIFIED = 0;
    JOB_STATUS_PENDING_DEPENDENCIES = 1;
    JOB_STATUS_QUEUED = 2;
    JOB_STATUS_RUNNING = 3;
    JOB_STATUS_COMPLETED = 4;
    JOB_STATUS_FAILED = 5;
  }
  JobStatus job_status = 1;
  float progress_percent = 2;
  string error_message = 3;
}
```

#### CancelJob

```protobuf
rpc CancelJob(CancelJobRequest) returns (CancelJobResponse);
```

---

### Internal API (Worker → Coordinator)

#### GetWorkStream

```protobuf
rpc GetWorkStream(GetWorkStreamRequest) returns (stream GetWorkStreamResponse);
```

**Request:**
```protobuf
message GetWorkStreamRequest {
  string worker_id = 1;
  repeated string capabilities = 2;  // e.g., ["skewer"], ["loom"]
}
```

**Response:**
```protobuf
message GetWorkStreamResponse {
  string job_id = 1;
  string task_id = 2;
  string frame_id = 3;
  oneof payload {
    RenderTask render_task = 4;
    CompositeTask composite_task = 5;
  }
}
```

#### ReportTaskResult

```protobuf
rpc ReportTaskResult(ReportTaskResultRequest) returns (ReportTaskResultResponse);
```

**Request:**
```protobuf
message ReportTaskResultRequest {
  string task_id = 1;
  string job_id = 2;
  string worker_id = 3;
  bool success = 4;
  string error_message = 5;
  string output_uri = 6;
  int64 execution_time_ms = 7;
}
```

---

## Code Generation

Generate Go code:

```bash
bash scripts/gen_proto.sh
```

This generates:
- `api/proto/coordinator/v1/coordinator.pb.go` - Protobuf messages
- `api/proto/coordinator/v1/coordinator_grpc.pb.go` - gRPC service stubs
