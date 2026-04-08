# Architecture Overview

Skewer is a distributed deep rendering system with three main components:

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│     CLI     │────▶│ Coordinator │────▶│   Workers   │
│  (skewer-)  │     │   (Go/gRPC) │     │ (C++/Skewer)│
│   cli       │     │             │     │ + Loom      │
└─────────────┘     └─────────────┘     └─────────────┘
       │                   │                   │
       │             ┌─────┴─────┐         ┌─────┴─────┐
       │             │    DAG    │         │ Deep EXR  │
       │             │  Scheduler│         │  Output   │
       │             └───────────┘         └───────────┘
       │
       ▼
┌─────────────┐
│   Scene     │
│   Files     │
└─────────────┘
```

## Components

| Component | Language | Description |
|-----------|----------|-------------|
| **CLI** | Go | User interface for submitting jobs |
| **Coordinator** | Go | Manages job DAG, schedules tasks, provisions workers |
| **Skewer** | C++ | Ray-tracing renderer with deep EXR support |
| **Loom** | C++ | Deep compositor for merging layers |

## Data Flow

1. **User** submits job via CLI with scene JSON
2. **Coordinator** breaks job into atomic tasks (frame chunks)
3. **Workers** pull tasks via gRPC stream, render deep EXRs
4. **Loom** composites final output from deep EXR layers
5. **Results** written to GCS or local filesystem

## Communication

- **gRPC** - Between Coordinator and Workers (protobuf-defined API)
- **GCS** - Heavy data (images) stored in Google Cloud Storage
- **URI Paths** - Lightweight metadata in protobuf messages

## See Also

- [Coordinator](coordinator.md) - Job scheduling and worker management
- [Skewer](skewer.md) - Renderer architecture
- [Loom](loom.md) - Compositor architecture
- [gRPC API](../api/grpc.md) - Protocol definition
