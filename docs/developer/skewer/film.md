# Film Architecture

The Film system is responsible for aggregating high-frequency radiance samples into a final image. It acts as the collection point for the Monte Carlo path tracer.

Importantly, the Film takes AoS (Array of Structures) output produced by the integrator and applies any necessary operations before converting into a SoA (Structure of Arrays) format for writing/export.

## Pixel Data Structure

Skewer stores pixel data in a highly optimized `Pixel` struct (`skewer/src/film/film.h`). 

```cpp
struct Pixel {
    RGB color_sum;
    float alpha_sum;
    float weight_sum;
    int sample_count;
    RGB color_sq_sum;
    std::atomic<int> deep_head;
};
```
### Memory Layout (Hot vs. Cold)
To maximize cache efficiency during the intensive path tracing loop, the `Pixel` struct groups all "hot" accumulation fields (`color_sum`, `color_sq_sum`, `sample_count`) at the beginning of the struct. The `deep_head` (an atomic integer used only for deep image linked lists) is placed at the end, as it is accessed far less frequently (only when paths terminate) or accessed cold during the final image assembly.

## The SampleWriter Pattern

To decouple the integrator from the film and reduce contention, Skewer uses a `SampleWriter` (`skewer/src/film/sample_writer.h`). 
- When an integrator begins evaluating a ray for a specific pixel, it instantiates a local `SampleWriter`.
- The `SampleWriter` buffers the final beauty pass values and any generated deep segments locally (`BoundedArray<DeepSegment>`).
- Once the path is fully resolved, the writer commits the data to the global `Film` object using `WriteBeauty` or `FlushDeepSegments`, minimizing the time spent modifying shared pixel state.

## Adaptive Sampling & Convergence

Skewer supports adaptive sampling to focus computational power on high-variance regions (noise).

### Variance Tracking
The film tracks both the sum of colors (`color_sum`) and the sum of squared colors (`color_sq_sum`). This allows the `IsPixelConverged` method to calculate the running variance of the pixel on the fly.

### Luminance Clamping (Cycles Approach)
Variance is evaluated based on human-perceived luminance (using Rec. 709 weights). A common issue in adaptive path tracers is that extremely dark pixels (where the mean luminance is near zero) will falsely report a massive relative variance, causing the renderer to waste samples there.
Skewer implements the "Cycles approach" by clamping the minimum mean luminance to `0.5f` in the denominator:
`noise = std::sqrt(var_lum / n) / std::max(mean_lum, 0.5f);`
This ensures dark pixels use an absolute noise threshold, while bright pixels use a relative noise threshold.

## Image Buffers

Once rendering is complete, the `Film` translates the raw accumulated pixel data into exportable formats using `CreateFlatBuffer`. This divides the accumulated `color_sum` by `weight_sum` to produce the final, converged premultiplied RGB values, along with an average coverage alpha. These buffers are then routed to `image_io` for saving as PNGs or flat OpenEXR files.
