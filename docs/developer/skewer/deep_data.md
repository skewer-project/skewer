# Deep Image System

Skewer features a native, highly concurrent pipeline for generating Deep EXR files, allowing volumetric data and transparent surfaces to be correctly composited in post-production tools like Nuke or Loom.

## Deep Path Recording

During the path trace, Skewer cannot immediately write segments to the film. The final radiance of a volumetric segment depends on the rest of the light path (Global Illumination), which isn't known until the path terminates.

### The Backward Pass
Skewer solves this using the `DeepPathRecorder` (`skewer/src/core/transport/deep_path_recorder.h`):

1. **Forward Path**: As the ray bounces through the scene, the recorder appends `PathVertex` objects containing local emission and BSDF weights.

2. **Backward Resolve**: Once the ray terminates, we iterate backwards from the end of the path to the camera.

3. **Radiance Accumulation**: We resolve the Rendering Equation at each vertex: $L_{out} = L_{local} + (\text{BSDF}_{weight} \cdot L_{incoming})$.

4. **Compositing Reset**: When a deep segment is extracted and pushed to the film, the internal accumulator (`deep_L`) is explicitly reset to `0.0f`. This is critical: if it were not reset, the background light would be embedded into the foreground segment, causing compositing software to double-add the background.

## Lock-Free Chunked Allocation

Rendering deep images generates millions of depth samples (segments) per frame. To handle this across dozens of threads without mutex contention, Skewer uses a **Deep Segment Pool** (`skewer/src/film/deep_segment_pool.h`).

### Memory Architecture
- **Chunked Pool**: Nodes are allocated in fixed-size blocks (chunks) to avoid massive upfront allocations.
- **Fast Path**: Threads allocate nodes using a lock-free `cursor_.fetch_add(1)`. A mutex is only engaged when the pool exhausts a chunk and needs to allocate a new one.
- **Atomic Linked Lists**: Each pixel in the film holds an `std::atomic<int> deep_head`. The `SampleWriter` pushes new segments to the pixel's list using `compare_exchange_weak`, ensuring thread-safe, lock-free segment insertion.

## Deep Image Assembly & OpenEXR Translation

After all threads finish rendering, the film performs a final assembly pass (`Film::BuildDeepImage`) to convert the raw, fragmented linked lists into a valid OpenEXR format.

### 1. Sequential Processing
To minimize memory overhead from sorting, the assembly processes the image scanline-by-scanline, reusing a small transient buffer.

### 2. Depth Sorting
Segments for each pixel are gathered and sorted primarily by `z_front` (and secondarily by `z_back`). This ordering is strictly required by the OpenEXR specification.

### 3. Stochastic Epsilon Merging
Because Monte Carlo path tracing is stochastic, paths that hit the same physical object will generate segments with very slightly different depth values. Skewer merges these segments if they fall within a 1.5% depth epsilon (`depth_epsilon = std::max(0.01f, std::abs(z_front) * 0.015f)`). 
*Note: Skewer explicitly prevents merging a hard surface sample with a volumetric sample, even if they share the exact same depth.*

### 4. True Opacity & Associated Alpha
For standard pixels, alpha is just coverage. For Deep EXR, opacity represents the fraction of light paths that were blocked at that specific depth.
Skewer calculates this by tracking the number of active Monte Carlo paths. The `true_opacity` of a segment is the number of paths that terminated in that segment divided by the total number of paths that survived to reach it. The color is then converted to "associated alpha" (premultiplied by the true opacity) before being written to disk.
