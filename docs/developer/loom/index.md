# Loom Compositor

Loom is a deep compositor designed to merge multiple high-resolution deep EXR layers into a single, flattened image. In the new architecture, Loom runs as a final stage **Cloud Batch** job.

## Architecture

### Execution Model: Cloud Batch

Loom is orchestrated by **Cloud Workflows** to run only after all required layers have been rendered by Skewer.

1. **Dependency Check** - Cloud Workflow ensures all `skewer-worker` tasks for a frame have succeeded.
2. **Job Spin-up** - Cloud Batch provisions an `e2-highmem-8` instance. High memory is critical for Loom as it must hold multiple deep EXR buffers simultaneously.
3. **Merging** - Loom reads the deep EXR layers via **GCS FUSE** (`/mnt/data`), performs the interval merge, and writes the final output.
4. **Cleanup** - The worker VM is terminated.

### Memory Management

Compositing deep EXR layers is extremely memory-intensive. Unlike standard images, deep pixels contain a variable number of samples. Loom uses a streaming row-based approach where possible, but still requires significant RAM for sorting and merging deep intervals.

#### Batch Profile

| Resource | Value | Rationale |
|----------|-------|-----------|
| **Machine Type** | `e2-highmem-8` | High RAM-to-CPU ratio for large deep images. |
| **CPU Milli** | `8000` | Full 8-core allocation for parallel pixel merging. |
| **Memory MiB** | `32768` | 32GB minimum to handle complex layered composites. |
| **Provisioning** | Standard | Avoids preemption during long merge operations. |

### Data I/O (GCS FUSE)

Loom accesses inputs via environment variables provided by the workflow:

| Variable | Description |
|----------|-------------|
| `LAYER_URI_PREFIXES` | Comma-separated GCS FUSE paths to the rendered layers. |
| `OUTPUT_URI_PREFIX` | GCS FUSE path for the final composited frame. |

## Deep EXR Format

Each layer render produces a deep EXR file with 6 channels per sample:

| Channel | Description |
|---------|-------------|
| `R`, `G`, `B` | Premultiplied color (rgb × alpha) |
| `A` | Alpha (opacity at this depth sample) |
| `Z` | Front depth (distance to camera) |
| `ZBack` | Back depth (far side of this sample) |

**Premultiplied colors** mean the RGB values are already multiplied by alpha. This is essential for correct compositing: `over` operations use `C_out = C_A + C_B × (1 - alpha_A)`.

Deep EXR stores multiple samples per pixel at different depths, enabling accurate compositing of overlapping geometry from different layers — even when objects interleave in Z.

Three outputs are produced by the compositing pipeline:

| Output | File | Description |
|--------|------|-------------|
| Flat EXR | `<prefix>_flat.exr` | Single-layer EXR (flattened deep data) |
| PNG | `<prefix>.png` | Final composited image (gamma-corrected) |
| Merged deep EXR | `<prefix>_merged.exr` | Full deep data with all layers combined |

## Algorithm

### The Volumetric Merge Problem

In standard 2D compositing, the "Over" operator assumes that object A is either in front of or behind object B. In deep compositing, objects (especially volumes like fog or fire) can **overlap in depth**.

If Layer A contains a cloud from $Z=10$ to $Z=50$ and Layer B contains a character at $Z=30$, a simple "Over" operator fails. The character must be embedded inside the cloud — occluding the back half of the cloud while being occluded by the front half.

### The 5-Step Merge Pipeline

Loom solves this by decomposing every deep pixel into a set of discrete, non-overlapping intervals.

#### 1. Boundary Collection

For a given pixel, Loom gathers every `z_front` and `z_back` value from every sample in every input layer. These values form a sorted set of "split points."

#### 2. Interval Splitting (`SplitSample`)

Every volumetric sample that spans multiple split points is cut into smaller "fragments."

!!! note "Design Decision"
    Loom uses the **Beer-Lambert Power Law** to calculate the alpha of these fragments, ensuring that the total transmittance of the fragments equals the transmittance of the original sample:
    $\alpha_{fragment} = 1 - (1 - \alpha_{original})^{\frac{\text{thickness}_{fragment}}{\text{thickness}_{original}}}$

#### 3. Depth Sorting

All fragments (and original hard-surface samples) are sorted by their `z_front` value.

#### 4. Uniform Interspersion (`BlendCoincidentSamples`)

Fragments that now share the exact same $[Z_{front}, Z_{back}]$ interval are merged. Loom assumes "Uniform Interspersion," meaning the particles from both layers are mixed evenly within that interval:

- **Combined Alpha**: $\alpha_{c} = 1 - (1 - \alpha_a)(1 - \alpha_b)$
- **Color Scaling**: To prevent over-brightening, the summed colors are scaled by the ratio of the new combined alpha to the sum of individual alphas.

#### 5. Final Flattening

Once the intervals are unique and sorted, the pixel is "flattened" into a standard RGBA value for preview using the recursive Over operator:
$C_{out} = C_{front} + (1 - \alpha_{front}) \cdot C_{back}$

### Execution Architecture

Loom is designed as a **Streaming Multi-threaded Pipeline** (`loom/src/deep_compositor.cc`) to handle multi-gigabyte EXR files without exhausting system RAM.

#### The Circular Window Buffer

Loom does not load entire images into memory. It uses a **48-scanline window**.

- **Loader Workers**: Read scanlines from disk into the window.
- **Merger Workers**: Perform the Interval Merge logic on the loaded rows.
- **Writer Workers**: Flatten the rows and write them to the final output file.

#### Thread Orchestration

Loom uses a "1-N-1" thread model:

- **1 Loader Thread**: Focused on I/O-bound disk reads.
- **N Merger Threads**: Focused on the CPU-intensive splitting and blending math.
- **1 Writer Thread**: Focused on I/O-bound disk writes.

This architecture ensures that the CPU cores are always saturated with math while the disk is constantly streaming data.

#### Merging Strategies

Loom implements two different merging paths:

- **`SortAndMergePixelsDirect`**: A fast path for simple scenes where samples don't overlap in depth. It skips the expensive interval splitting.
- **`SortAndMergePixelsWithSplit`**: The full physically-correct path required for volumetric interspersion.

## Common Issues

### Missing Layers

If a layer render fails, the compositing step will also fail. The cloud workflow raises an error with the layer name and failure state. Check the failed Batch job logs to diagnose.

### Mismatched Resolutions

All layers must render at the same resolution. If one layer has a different `image.width` or `image.height`, the compositor will produce incorrect results or fail. Ensure the highest-priority layer (typically the first context or layer file) sets the resolution, or override it uniformly in the workflow configuration.

### NaN in Deep Samples

If a layer produces NaN values in its deep EXR output, the compositor will propagate them to the final image. Use the `normals` integrator on the problematic layer to check geometry, and verify material parameters (especially IOR and roughness on dielectrics).

## See Also

- [Architecture Overview](../overview.md) - System-level architecture
- [Skewer Renderer](../skewer/architecture.md) - The renderer providing the deep EXR inputs
- [CLI Reference](../../reference/cli.md) - `loom` binary usage
- [GCP Deployment](../../getting-started/gcp.md) - Batch profile configurations
