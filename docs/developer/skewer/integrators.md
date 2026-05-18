# Path Tracing & Integrators

Integrators are responsible for simulating the transport of light throughout the scene. Skewer’s primary integrator is a physically-based, iterative CPU path tracer designed for high-fidelity global illumination.

Rendering is a trade-off between **Variance** (noise) and **Bias** (systematic error). Skewer's path tracer is fundamentally unbiased. Given enough samples, it will converge to the mathematically "correct" solution. Techniques like **NEE** and **MIS** are implemented to reduce variance as aggressively as possible without introducing bias. This allows the engine to produce clean images in a fraction of the time required by a naive path tracer.

### Performance Through Locality
Modern CPUs are memory-bound. The integrator's use of **Tile-Based Rendering** and **Thread-Local Buffering** (via `SampleWriter`) is a deliberate architectural choice to maximize CPU cache hits and minimize atomic contention on shared memory.

---

## Directory Reference

The following sections detail the implementations within the `skewer/src/integrators/` directory.

### Base Interface

This header defines the `Integrator` abstract base class.

- **Polymorphism Decision**: Skewer uses a standard C++ virtual interface for the high-level `Render()` call. This allows the engine to switch between different rendering modes (Path Tracing, Normal Visualization, Debug) at runtime.
- **CPU vs GPU**: This abstract class architecture is specific to the **CPU implementation**. Because CPU rendering involves coarse-grained work distribution (tiles to threads), virtual calls at this level have negligible overhead. In a future **GPU implementation**, this polymorphism would likely be removed in favor of direct kernel dispatch to avoid the massive performance penalties of vtables on many-core architectures.

### Path Tracer

The `PathTrace` class manages the high-level rendering loop and resource distribution.

#### Work Orchestration (Tile-Based Distribution)
Skewer divides the image into $32 \times 32$ pixel tiles. 

- **Cache Locality**: By focusing a thread on a small spatial region, we maximize the chances that the BVH nodes and textures required for that area stay in the CPU's L2/L3 cache.
- **Adaptive Break**: The integrator checks `film->IsPixelConverged()` every `adaptive_step` (default 16 samples). If a pixel’s variance is below the `noise_threshold`, the loop breaks early, reallocating compute power to "difficult" regions like caustics or deep shadows.

### Normals

A utility integrator used for "Look-Dev" and debugging. It bypasses the complex path tracing logic to visualize the geometric or shading normals of the scene directly as colors. This is essential for verifying UV mapping and normal map orientation before committing to a full spectral render.
