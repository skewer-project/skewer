# Film & Deep Data

The Film system is responsible for aggregating high-frequency radiance samples into a final image or deep data structure.

## Lock-Free Deep Memory Management

Rendering deep images requires storing millions of depth samples (segments) per frame. To handle this in a highly concurrent environment, Skewer uses a **Deep Segment Pool** (`skewer/src/film/deep_segment_pool.h`).

### Chunked Allocation
Instead of one massive upfront allocation, the pool uses a **Chunked Pool Allocator**.
- **Chunks**: Nodes are allocated in fixed-size blocks (chunks).
- **Scalability**: Threads use an `std::atomic` cursor for fast-path allocation. A mutex is only engaged when the pool needs to grow (allocate a new chunk).

### Atomic Linked Lists
Each pixel in the film contains an `std::atomic<int> deep_head`, which is an index into the pool.
- When a path tracer thread completes a sample, it pushes the new segments to the pixel using `compare_exchange_weak`.
- This creates a per-pixel linked list of deep segments that is entirely **lock-free** during the primary path tracing loop.

## Deep Path Recording

During the path trace, Skewer cannot immediately write segments to the film because the final radiance of a segment depends on the rest of the light path (Global Illumination).

### The Backward Pass
Skewer uses a `DeepPathRecorder` (`skewer/src/core/transport/deep_path_recorder.h`) to solve this:
1. **Forward Path**: As the ray bounces, we record `PathVertex` objects (local emission and BSDF weights).
2. **Backward Resolve**: Once the path terminates, we iterate backwards from the end of the path to the camera.
3. **Radiance Accumulation**: We resolve the Rendering Equation at each vertex: $L_{out} = L_{local} + (\text{BSDF}_{weight} \cdot L_{incoming})$.
4. **Segment Extraction**: Only vertices still on the "Camera Path" (those that haven't been deflected by a non-transparent event) are pushed to the film as deep segments.

## Deep Image Assembly

Once rendering is complete, the Film performs a final assembly pass (`BuildDeepImage`) to convert the raw linked lists into an OpenEXR-compatible format:
1. **Depth Sorting**: Segments for each pixel are collected and sorted by `z_front`.
2. **Stochastic Merging**: Since path tracing is stochastic, many segments might overlap. Skewer uses a depth-based **epsilon** to group and merge segments that are physically close, reducing file size.
3. **True Opacity Calculation**: The opacity of a deep segment is calculated as the fraction of Monte Carlo paths that terminated at that depth versus the total number of paths that reached that depth.
