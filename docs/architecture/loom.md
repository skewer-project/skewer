# Loom Compositor

Loom is a deep compositor designed to merge multiple high-resolution deep EXR layers into a single, flattened image. In the new architecture, Loom runs as a final stage **Cloud Batch** job.

## Execution Model: Cloud Batch

Loom is orchestrated by **Cloud Workflows** to run only after all required layers have been rendered by Skewer.

1. **Dependency Check** - Cloud Workflow ensures all `skewer-worker` tasks for a frame have succeeded.
2. **Job Spin-up** - Cloud Batch provisions an `e2-highmem-8` instance. High memory is critical for Loom as it must hold multiple deep EXR buffers simultaneously.
3. **Merging** - Loom reads the deep EXR layers via **GCS FUSE** (`/mnt/data`), performs the interval merge, and writes the final output.
4. **Cleanup** - The worker VM is terminated.

## Memory Management

Compositing deep EXR layers is extremely memory-intensive. Unlike standard images, deep pixels contain a variable number of samples. Loom uses a streaming row-based approach where possible, but still requires significant RAM for sorting and merging deep intervals.

### Batch Profile

| Resource | Value | Rationale |
|----------|-------|-----------|
| **Machine Type** | `e2-highmem-8` | High RAM-to-CPU ratio for large deep images. |
| **CPU Milli** | `8000` | Full 8-core allocation for parallel pixel merging. |
| **Memory MiB** | `32768` | 32GB minimum to handle complex layered composites. |
| **Provisioning** | Standard | Avoids preemption during long merge operations. |

## The Interval Merge Algorithm

Loom's core logic remains focused on the correct handling of overlapping volumes:

1. **Boundary Collection** - Gathers depth boundaries from all input layers.
2. **Interval Generation** - Creates non-overlapping depth segments.
3. **Sample Splitting** - Adjusts alpha values using the Alpha Power Law.
4. **Normalization** - Sorts and prepares samples for flattening.
5. **Flattening** - Applies the front-to-back "Over" operator.

## Data I/O (GCS FUSE)

Loom accesses inputs via environment variables provided by the workflow:

| Variable | Description |
|----------|-------------|
| `LAYER_URI_PREFIXES` | Comma-separated GCS FUSE paths to the rendered layers. |
| `OUTPUT_URI_PREFIX` | GCS FUSE path for the final composited frame. |

## See Also

- [Architecture Overview](overview.md) - System-level architecture
- [Skewer](skewer.md) - The renderer providing the deep EXR inputs
- [GCP Deployment](../deployment/gcp.md) - Batch profile configurations
