# Skewer Renderer

Skewer is a high-performance, CPU-based ray-tracing renderer designed to run as an ephemeral worker in **Cloud Batch**.

## Key Features

- **Path Tracing** - Physically-based ray tracing with importance sampling.
- **Deep EXR** - Per-pixel opacity as a linear-piecewise function of depth.
- **Adaptive Sampling** - Focuses compute on high-variance regions of the image.
- **GCS FUSE Integration** - Mounts cloud storage directly for high-throughput I/O.
- **Serverless Execution** - Optimized for cold starts in Cloud Batch containers.

## Architecture

```
Cloud Batch Job
      │
      ▼
┌─────────────┐     ┌─────────────┐
│  GCS FUSE   │────▶│ Scene Loader│
│  /mnt/data  │     └──────┬──────┘
└─────────────┘            │
                           ▼
┌─────────────┐     ┌─────────────┐
│   BVH       │────▶│  Integrator │
│ Acceleration│     │ (Path Trace)│
└─────────────┘     └──────┬──────┘
                           │
                           ▼
                   ┌─────────────┐
                   │    Film     │
                   │(Deep EXR)   │
                   └──────┬──────┘
                          │
                          ▼
                   ┌─────────────┐
                   │  GCS FUSE   │
                   │ /mnt/cache  │
                   └─────────────┘
```

## Execution Model: Cloud Batch

Skewer workers are no longer persistent. They are executed as **Batch Task Groups**:

1. **Job Initialization** - Cloud Batch spins up an `n2d-highcpu-8` Spot VM.
2. **Mounting** - GCS FUSE automatically mounts the data and cache buckets to `/mnt/`.
3. **Rendering** - The `skewer-worker` container starts, reads the scene from `/mnt/data`, and renders.
4. **Caching** - Intermediate layer results are written to `/mnt/cache` using content-hash keys.
5. **Termination** - The VM is immediately released upon task completion.

## Command Line (Batch Mode)

When running in Cloud Batch, Skewer is typically invoked with specific environment variables passed by the workflow:

| Variable | Description |
|----------|-------------|
| `SCENE_URI` | Path to the scene JSON (e.g., `/mnt/data/scenes/forest.json`). |
| `OUTPUT_URI_PREFIX` | Where to write the final deep EXR. |
| `CACHE_PREFIX` | Location in the cache bucket to check before rendering. |
| `NUM_FRAMES` | Total frames in the current job sequence. |

## Performance Profiles

| Profile | Machine Type | Provisioning | Use Case |
|---------|--------------|--------------|----------|
| **Standard** | `n2d-highcpu-8` | Spot | High-throughput, cost-optimized rendering. |
| **High-Mem** | `n2-highmem-16` | Standard | Extremely complex scenes with large BVHs. |

## See Also

- [Architecture Overview](overview.md) - System-level architecture
- [Loom](loom.md) - Deep compositing worker
- [GCS FUSE](../deployment/gcp.md#storage) - How workers access cloud data
