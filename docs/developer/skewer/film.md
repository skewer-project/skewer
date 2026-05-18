# Film Architecture


The Film system is responsible for aggregating high-frequency radiance samples into a final image. It acts as the collection point for the Monte Carlo path tracer.

Importantly, the Film takes AoS (Array of Structures) output produced by the integrator and applies any necessary operations before converting into a SoA (Structure of Arrays) format for writing/export.

---

## Directory Reference

The following sections detail the implementations within the `skewer/src/film/` directory.

### Film

The `Film` class is the central manager for all pixel data. It maintains a large buffer of `Pixel` structs and provides thread-safe methods for adding samples.

#### Pixel Data Structure
Skewer stores pixel data in a highly optimized `Pixel` struct. 

```cpp
struct Pixel {
    RGB color_sum;
    float alpha_sum;
    float weight_sum;
    int sample_count;
    RGB color_sq_sum;
    SmallVector<DeepBucket, 1> deep_buckets;
};
```

#### Memory Layout (Hot vs. Cold)
To maximize cache efficiency during the intensive path tracing loop, the `Pixel` struct groups all "hot" accumulation fields (`color_sum`, `color_sq_sum`, `sample_count`) at the beginning of the struct. The `deep_buckets` (which involve complex vector logic) are placed at the end, as they are accessed far less frequently (only when paths terminate) or accessed cold during the final image assembly.

#### Adaptive Sampling & Convergence
The film tracks luminance-weighted variance to drive adaptive sampling.

- **Luminance Clamping**: Variance is evaluated using Rec. 709 weights. Skewer implements a clamping mechanism where the minimum mean luminance is fixed to `0.5f` in the denominator. This ensures that dark pixels use an absolute noise threshold, preventing the renderer from wasting samples on visually insignificant regions.

### Sample Writer

To decouple the integrator from the film and reduce contention, Skewer uses a `SampleWriter`. 

- **Local Buffering**: When an integrator evaluates a ray, it uses a local `SampleWriter` to buffer the beauty pass values and generated deep segments.
- **Atomic Commits**: Once the path is resolved, the writer commits the data to the global `Film` object, minimizing the time spent modifying shared memory.

### Deep Buckets

Deep rendering produces millions of samples. To manage this without exploding memory, Skewer uses **Deep Buckets** for online aggregation.

- **`DeepAlphaClass`**: Distinguishes between hard surfaces (alpha = 1.0) and volumes (fractional alpha). Skewer explicitly prevents merging these different classes to preserve depth-compositing correctness.
- **Online Merging**: Instead of storing every single stochastic sample, the film attempts to merge incoming segments into existing buckets if they are within a depth epsilon. This acts as a high-performance, lossy compression pass performed during the render.

### Image Buffers

The `ImageBuffer` classes handle the final translation of accumulated data into formats suitable for disk I/O.

- **`FlatImageBuffer`**: A standard SoA layout for RGB and Alpha channels. It handles the final division of `color_sum / weight_sum` and the application of exposure/gamma if necessary.
- **`DeepImageBuffer`**: A high-performance container that packs all merged deep samples into a single contiguous block of memory (Compressed Row Storage style). This layout maximizes cache locality during the multi-layer merge process in Loom.
