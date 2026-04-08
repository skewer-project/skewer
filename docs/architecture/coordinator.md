# Coordinator

The Coordinator is the "brain" of the Skewer distributed rendering system. It's a Go-based service that manages job orchestration, worker scheduling, and cluster state.

## Responsibilities

### 1. Job Orchestration

The Coordinator takes high-level render jobs and breaks them into atomic tasks:

- **Task Generation** - Splits frames into renderable chunks
- **Scheduling** - Assigns tasks to available workers
- **Retries** - Re-queues failed tasks for another worker

### 2. Worker Management

- **Registration** - Workers connect on startup
- **Heartbeats** - Workers send periodic updates; missed heartbeats trigger rescheduling
- **State Tracking** - Maintains cluster state as source of truth

### 3. Job Tracking

- **Progress Aggregation** - Reports job completion percentage
- **Status API** - CLI queries job status via gRPC

## Architecture

```
Job Submit ─▶ DAG Builder ─▶ Scheduler ─▶ Workers
                │              │
                ▼              ▼
            Job Queue      Task Queue
```

### Key Components

| Component | Description |
|-----------|-------------|
| **DAG Builder** | Constructs dependency graph from job layers |
| **Scheduler** | Assigns tasks to workers based on capacity |
| **Cloud Manager** | Provisions GKE resources (BYOP) |
| **Job Tracker** | Tracks in-progress jobs and tasks |

## Running the Coordinator

```bash
# Build
go build -o coordinator ./orchestration/cmd/coordinator/

# Run locally
./coordinator --port 50051 --local-storage /path/to/data
```

## Configuration

| Flag | Description | Default |
|------|-------------|---------|
| `--port` | gRPC listen port | `50051` |
| `--local-storage` | Base path for local files | `./data` |
| `--gcp-project` | GCP project for BYOP | (none) |

## See Also

- [gRPC API](../api/grpc.md) - Protocol definitions
- [Local Deployment](../deployment/local.md) - Running locally with K8s
- [GKE Deployment](../deployment/gke.md) - Running on Google Kubernetes Engine
