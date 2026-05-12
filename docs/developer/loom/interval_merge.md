# Interval Merge Algorithm

Loom's primary responsibility is merging deep data from multiple independent layers into a single, physically correct deep image. This is accomplished using the **Interval Merge Algorithm** (`loom/src/deep_merger.cc`) and a highly optimized, streaming multi-threaded pipeline.

## The Volumetric Merge Problem

In standard 2D compositing, the "Over" operator assumes that object A is either in front of or behind object B. In deep compositing, objects (especially volumes like fog or fire) can **overlap in depth**. 

If Layer A contains a cloud from $Z=10$ to $Z=50$ and Layer B contains a character at $Z=30$, a simple "Over" operator fails. The character must be embedded inside the cloud—occluding the back half of the cloud while being occluded by the front half.

## The 5-Step Merge Pipeline

Loom solves this by decomposing every deep pixel into a set of discrete, non-overlapping intervals.

### 1. Boundary Collection
For a given pixel, Loom gathers every `z_front` and `z_back` value from every sample in every input layer. These values form a sorted set of "split points."

### 2. Interval Splitting (`SplitSample`)
Every volumetric sample that spans multiple split points is cut into smaller "fragments." 
- **Design Decision**: Loom uses the **Beer-Lambert Power Law** to calculate the alpha of these fragments, ensuring that the total transmittance of the fragments equals the transmittance of the original sample:
  $\alpha_{fragment} = 1 - (1 - \alpha_{original})^{\frac{\text{thickness}_{fragment}}{\text{thickness}_{original}}}$

### 3. Depth Sorting
All fragments (and original hard-surface samples) are sorted by their `z_front` value.

### 4. Uniform Interspersion (`BlendCoincidentSamples`)
Fragments that now share the exact same $[Z_{front}, Z_{back}]$ interval are merged. Loom assumes "Uniform Interspersion," meaning the particles from both layers are mixed evenly within that interval:
- **Combined Alpha**: $\alpha_{c} = 1 - (1 - \alpha_a)(1 - \alpha_b)$
- **Color Scaling**: To prevent over-brightening, the summed colors are scaled by the ratio of the new combined alpha to the sum of individual alphas.

### 5. Final Flattening
Once the intervals are unique and sorted, the pixel is "flattened" into a standard RGBA value for preview using the recursive Over operator:
$C_{out} = C_{front} + (1 - \alpha_{front}) \cdot C_{back}$

## Execution Architecture

Loom is designed as a **Streaming Multi-threaded Pipeline** (`loom/src/deep_compositor.cc`) to handle multi-gigabyte EXR files without exhausting system RAM.

### 1. The Circular Window Buffer
Loom does not load entire images into memory. It uses a **48-scanline window**. 
- **Loader Workers**: Read scanlines from disk into the window.
- **Merger Workers**: Perform the Interval Merge logic on the loaded rows.
- **Writer Workers**: Flatten the rows and write them to the final output file.

### 2. Thread Orchestration
Loom uses a "1-N-1" thread model:
- **1 Loader Thread**: Focused on I/O-bound disk reads.
- **N Merger Threads**: Focused on the CPU-intensive splitting and blending math.
- **1 Writer Thread**: Focused on I/O-bound disk writes.
This architecture ensures that the CPU cores are always saturated with math while the disk is constantly streaming data.

### 3. Merging Strategies
Loom implements two different merging paths:
- **`SortAndMergePixelsDirect`**: A fast path for simple scenes where samples don't overlap in depth. It skips the expensive interval splitting.
- **`SortAndMergePixelsWithSplit`**: The full physically-correct path required for volumetric interspersion.
