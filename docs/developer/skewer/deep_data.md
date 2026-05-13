# Deep Image System

Skewer features a native, highly concurrent pipeline for generating Deep EXR files. Because deep rendering fundamentally alters how light is recorded, it touches almost every aspect of the engine — from the lowest-level integrators to the final film assembly.

## 1. Conceptual Overview

In a standard "flat" image, a pixel stores a single RGBA value representing the *total* accumulated light hitting the camera sensor. In a **Deep Image**, a pixel stores a list of *multiple* RGBA values. More specifically, it stores a list of discrete "segments" (also called intervals, or samples), where each segment has a depth range (`z_front` to `z_back`), a color, and an opacity.

Rather than representing the *total* accumulated light hitting the camera, each deep segment represents the amount of *contributing* light up to that point.

### The "Camera Path" Invariant
For a deep image to correctly composite in software like Nuke, it must represent the "line of sight" from the camera. Therefore, **only the straight-line segments of a path before it deflects off an opaque or refractive surface are recorded**. 

- If a ray travels through fog (scattering forward), it remains on the Camera Path.
- If a ray hits a wall and bounces to hit a light source, only the segment from the camera to the wall is recorded. The light from the bounce is considered "Global Illumination" and is embedded into the color of that initial segment.

## 2. Design Philosophy & Key Decisions

### Stochastic Scattering vs. Ray Marching
A common approach to volume rendering is **Ray Marching**—stepping along a ray at fixed intervals and accumulating density/color. Skewer **does not** do this.

Instead, Skewer uses **Stochastic Scattering** (Woodcock Tracking). The integrator treats volumes exactly like hard surfaces, but the "surface" is a probabilistically chosen point in space.

- **Why?**: This unifies the mathematical pipeline. The path tracer doesn't care if it hit a brick wall or a water droplet in a cloud. It simply evaluates the BSDF/Phase Function, estimates direct lighting (NEE), and bounces. 
- **Deep Benefit**: This naturally generates discrete points in space, which are perfectly suited for conversion into deep segments, completely eliminating the need for a separate volumetric "marching and accumulation" pass during integration.

The important consequence is that **every recorded deep sample is essentially a point sample**. The "deep interval" is created post-render, where multiple closely-spaced deep samples are merged into a true interval in space.

## 3. Cross-Component Lifecycle (The Path of a Segment)

The generation of a deep segment spans multiple systems.

### Step A: The Integrator (`path_kernel.cc`)
During the `Li()` tracing loop, the kernel tracks whether the current ray is still on the "Camera Path." At every interaction (surface or volume), it pushes raw vertex data (depth, local emission, BSDF weights) to a local recorder, rather than immediately adding color to a pixel.

### Step B: The Transport Layer (`DeepPathRecorder`)
Because Global Illumination (light arriving from bounces later in the path) affects the color of the segments closer to the camera, segments cannot be finalized on the way *forward*.
Skewer solves this with a **Deferred Backward Pass**:

1. Once the ray terminates, the `DeepPathRecorder` iterates backwards from the end of the path to the camera.
2. It resolves the Rendering Equation at each vertex: $L_{out} = L_{local} + (\text{BSDF}_{weight} \cdot L_{incoming})$.
3. **Compositing Reset**: When a deep segment is extracted, the internal accumulator (`deep_L`) is explicitly reset to `0.0f`. If it were not reset, the background light would be embedded into the foreground segment, causing compositing software to double-add the background.

### Step C: The Storage Layer (`DeepSegmentPool`)
Millions of segments are generated per frame. To handle this across dozens of threads without mutex contention, Skewer uses a lock-free memory architecture in the Film:

- **Chunked Pool**: Nodes are allocated in fixed-size blocks using a lock-free `fetch_add` cursor.
- **Atomic Linked Lists**: The `SampleWriter` pushes new segments to a pixel's `std::atomic<int> deep_head` using `compare_exchange_weak`, ensuring thread-safe, lock-free insertion during the hot path.

## 4. The Assembly Pipeline

After all threads finish rendering, the film performs a final assembly pass (`Film::BuildDeepImage`) to convert the fragmented linked lists into a valid OpenEXR format.

### Sequential Processing
To minimize memory overhead from sorting, the assembly processes the image scanline-by-scanline, reusing a small transient buffer.

### Depth Sorting
Segments for each pixel are gathered and sorted primarily by `z_front` (and secondarily by `z_back`). This strictly ascending depth order is required by the OpenEXR specification.

### Stochastic Epsilon Merging
Because Monte Carlo path tracing is stochastic, thousands of paths hitting the same physical object will generate segments with very slightly different depth values. Skewer compresses these by merging segments that fall within a 1.5% depth epsilon (`depth_epsilon = std::max(0.01f, std::abs(z_front) * 0.015f)`). 

!!! note "Different Sample Types"
    Skewer explicitly prevents merging a hard surface sample with a volumetric sample, preserving depth boundaries.

### True Opacity & Associated Alpha
For standard pixels, alpha is just coverage. For Deep EXR, opacity represents the fraction of light paths that were blocked at that specific depth.

Skewer calculates this by tracking the number of active Monte Carlo paths in the pixel. The `true_opacity` of a segment is the number of paths that terminated in that segment divided by the total number of paths that survived to reach it. The color is then converted to "associated alpha" (premultiplied by the true opacity) before being written to disk.
