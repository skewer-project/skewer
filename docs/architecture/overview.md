# Architecture Overview

Skewer is a serverless, distributed deep rendering system with three main components orchestrated on Google Cloud Platform:

```
┌─────────────┐     ┌───────────────┐     ┌────────────────┐
│     CLI     │────▶│  Coordinator  │────▶│ Cloud Workflow │
│  (skewer-)  │     │  (Cloud Run)  │     │(Orchestration) │
│   cli       │     │               │     └────────┬───────┘
└─────────────┘     └───────────────┘              │
       │                    │                      │
       │              ┌─────┴─────┐        ┌───────┴───────┐
       │              │ Validation│        │  Cloud Batch  │
       │              │     &     │        │    Workers    │
       │              │ Submission│        │ (C++/Skewer)  │
       │              └───────────┘        └───────────────┘
       │                                           │
       ▼                                           │
┌─────────────┐                             ┌──────┴──────┐
│   Scene     │                             │  GCS FUSE   │
│   Files     │                             │  Data/Cache │
└─────────────┘                             └─────────────┘
```

## Components

| Component | Platform | Description |
|-----------|----------|-------------|
| **CLI** | Go (Local) | User interface for submitting jobs and tracking progress. |
| **Coordinator** | Cloud Run (Go) | Stateless API that validates jobs and initiates Cloud Workflow executions. |
| **Workflow** | Cloud Workflows | Managed DAG that orchestrates parallel rendering and compositing tasks. |
| **Workers** | Cloud Batch (C++) | Ephemeral VM-based workers (Skewer for rendering, Loom for compositing). |

## Data Flow

1. **User** submits job via CLI with scene JSON.
2. **Coordinator** (Cloud Run) validates the job and triggers a **Cloud Workflow** execution.
3. **Cloud Workflow** shatters the job into parallel **Cloud Batch** tasks (one per layer/frame).
4. **Cloud Batch** spins up worker VMs:
    * **Skewer** workers render deep EXR layers.
    * **Loom** workers composite the final output from rendered layers.
5. **Storage** is handled via **GCS FUSE**; workers mount `gs://` buckets to `/mnt/` for POSIX access.
6. **Results** are written back to the GCS Data Bucket.

## Communication

- **gRPC / HTTPS** - Between CLI, Coordinator, and Cloud Workflows.
- **Cloud Batch API** - Used by Workflows to manage worker lifecycles.
- **GCS FUSE** - Used by workers for high-throughput data I/O.
- **Artifact Registry** - Hosts multi-stage Docker images for all components.

## See Also

- [Coordinator](coordinator.md) - Submission and validation layer
- [Skewer](skewer.md) - Renderer architecture and Batch profile
- [Loom](loom.md) - Compositor architecture and Batch profile
- [GCP Deployment](../deployment/gcp.md) - Infrastructure and Terraform
