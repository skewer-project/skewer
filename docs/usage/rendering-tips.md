# Rendering Best Practices

This guide covers practical tips for producing high-quality renders efficiently with Skewer. It assumes you are familiar with the [Scene Format](scene-format.md).

## Lighting and Materials

### Emissive Light Sources

The simplest way to light a scene is with emissive materials. Set a material's `emission` to a color with values greater than 1.0 for HDR brightness:

```json
"sun_mat": {
  "type": "lambertian",
  "albedo": [0, 0, 0],
  "emission": [2, 1.4, 0.6]
}
```

- **Higher emission values** produce brighter light but increase variance (noise) because rays are unlikely to hit small bright surfaces by chance
- **Emissive spheres** (small radius, high emission) produce more fireflies than emissive quads or large surfaces
- To reduce noise from small emissive objects, increase `max_samples` or use a **larger emissive area** with lower emission values (same total light output, less variance)

### Material Choices by Surface Type

| Surface | Recommended Type | Typical Settings |
|---------|-----------------|------------------|
| Matte paint, fabric, wood | `lambertian` | `albedo: [0.8, 0.2, 0.2]`, no roughness |
| Polished metal, chrome | `metal` | `albedo: [1, 0.84, 0]`, `roughness: 0.05` |
| Brushed metal | `metal` | `albedo: [0.9, 0.9, 0.9]`, `roughness: 0.3` |
| Glass window | `dielectric` | `ior: 1.5`, `roughness: 0.0` |
| Frosted glass | `dielectric` | `ior: 1.5`, `roughness: 0.2` |
| Water | `dielectric` | `ior: 1.33`, `roughness: 0.01` |
| Diamond | `dielectric` | `ior: 2.42`, `roughness: 0.0` |

### Textures

Skewer supports three texture maps per material:

| Texture | Supported On | Purpose |
|---------|-------------|---------|
| `albedo_texture` | All types | Surface color variation |
| `normal_texture` | All types | Surface detail via perturbed normals |
| `roughness_texture` | Metal, Dielectric | Spatially varying microfacet roughness |

Texture paths are resolved **relative to the layer file's directory**. For cloud rendering, ensure textures are uploaded alongside the scene.

### Avoiding Pure Black Albedos on Non-Emissive Materials

A `lambertian` material with `albedo: [0, 0, 0]` and no emission will absorb all light and appear black. This is fine for light absorbers but can create harsh shadows. Use a very dark gray (`[0.01, 0.01, 0.01]`) instead for "black" surfaces that still reflect a tiny amount of light.

## Depth of Field

Skewer uses a thin-lens DOF model controlled by two camera parameters:

```json
"camera": {
  "look_from": [0, 2, 5],
  "look_at": [0, 0, 0],
  "focus_distance": 5.0,
  "aperture_radius": 0.15
}
```

| Parameter | Effect |
|-----------|--------|
| `aperture_radius: 0` | Pinhole camera, everything in focus |
| `aperture_radius: 0.05-0.1` | Subtle blur, shallow depth of field |
| `aperture_radius: 0.15-0.5` | Noticeable blur, bokeh effect |
| `aperture_radius: 1.0+` | Strong blur, only focus plane is sharp |

- `focus_distance` is the distance from the camera where objects are perfectly sharp. It defaults to `1.0`.
- The view frustum automatically scales so that `look_at` is at `focus_distance` — this means if you set `focus_distance` far from the `look_at` distance, the framing will shift.
- **DOF increases noise** significantly because each sample uses a different ray origin. Increase `max_samples` or use adaptive sampling when DOF is enabled.

## Motion Blur

Motion blur is controlled by the camera's shutter interval:

```json
"camera": {
  "shutter_open": 0.0,
  "shutter_close": 0.1
}
```

- Each ray sample gets a random time within the shutter interval
- Animated objects are evaluated at their ray time, producing motion blur
- **The shutter interval should match your animation timing** — if an object moves from A to B over `time: 0` to `time: 1`, a shutter interval of `[0, 0.1]` captures 1/10th of the motion
- For **no motion blur**, set both to the same value (the default is `0.0` / `0.0`)
- Motion blur increases noise similarly to DOF — more samples are needed

## Noise Reduction

### Adaptive Sampling

Adaptive sampling stops sampling pixels that have converged, saving time on simple regions (flat walls, sky) while continuing to sample complex regions (edges, shadows, caustics):

```json
"render": {
  "max_samples": 4096,
  "min_samples": 64,
  "noise_threshold": 0.02,
  "adaptive_step": 32
}
```

| Parameter | Recommended | Effect |
|-----------|------------|--------|
| `noise_threshold` | `0.01` - `0.05` | Lower = stricter convergence, more samples. `0` disables adaptive sampling |
| `min_samples` | `32` - `128` | Minimum samples before checking convergence. Higher = more reliable initial estimate |
| `adaptive_step` | `16` - `64` | How often to check convergence. Smaller = more frequent checks (more overhead but faster convergence) |
| `max_samples` | `512` - `8192` | Upper bound. Some pixels may never converge (fireflies, caustics) |

**How convergence works:** Skewer measures the luminance variance across samples per pixel. When `noise / max(mean_luminance, 0.5) < noise_threshold`, the pixel is considered converged. The 0.5 luminance floor prevents near-black pixels from requiring excessive samples.

### Debugging Noise

To see which regions are under-sampled, enable the sample map:

```json
"render": {
  "save_sample_map": true
}
```

This writes a heatmap image showing per-pixel sample counts. Dark regions sampled to completion; bright regions hit `max_samples` without converging.

### Fireflies

Fireflies (bright isolated pixels) are typically caused by:

1. **Caustics** — light focused through specular surfaces (glass, mirrors). Unidirectional path tracing struggles with these
2. **Small bright emissive surfaces** — low probability of NEE (next event estimation) hits produces high-weight samples
3. **Rough metals at grazing angles** — microfacet sampling can produce rare high-energy paths

**Mitigations:**
- Increase `max_samples` — adaptive sampling helps but won't fully eliminate fireflies
- Use `roughness > 0.01` on metals to spread reflections
- Avoid placing small emissive objects near reflective surfaces
- The Reinhard tonemapping (`color / (1 + color)`) applied during output compression reduces but does not eliminate fireflies

## Ray Depth

The `max_depth` parameter controls how many surface interactions a ray can undergo:

```json
"render": {
  "max_depth": 5
}
```

| max_depth | Result |
|-----------|--------|
| 1 | Direct lighting only — no reflections, no refractions, no indirect illumination |
| 2-3 | One bounce of indirect light, single reflections visible |
| 4-6 | Good quality for most scenes — multiple bounces, some caustics |
| 8-12 | High quality — glass-through-glass, mirror-mirror reflections |
| 20+ | Diminishing returns unless scene has many nested transparent surfaces |

Beyond `max_depth`, rays are terminated. Russian Roulette takes over after depth 3, probabilistically terminating paths with very low throughput (`< 0.001`).

## Performance Optimization

### Thread Count

```json
"render": {
  "threads": 0
}
```

- `0` (default) auto-detects CPU cores via `hardware_concurrency()`
- Set to a specific number to leave cores free for other tasks
- Performance scales roughly linearly up to physical core count, then plateaus

### Tile Size

```json
"render": {
  "tile_size": 32
}
```

- Tiles partition the image for parallel processing with work-stealing
- **Larger tiles (64-128):** Better cache coherence for BVH traversal, worse load balancing across threads
- **Smaller tiles (8-16):** Better load balancing, more atomic contention overhead
- Default `32` is a good balance for most scenes. Use smaller tiles for highly non-uniform scenes (complex geometry in some areas, empty sky in others)

### Resolution

Render time scales **quadratically** with resolution. A 4K image (3840×2160) has ~9× the pixels of 1080p (1920×1080) and will take roughly 9× longer at the same sample count.

**Workflow tip:** Render at lower resolution (`width`/`height` in render settings) during development, then switch to final resolution for the production render.

### Integrator Choice

```json
"render": {
  "integrator": "normals"
}
```

- `path_trace` (default): Full Monte Carlo path tracing with all lighting effects
- `normals`: Single-pass visualizes geometry normals as color. Useful for debugging mesh orientation and UV mapping. Runs instantly (no sampling)

## Debugging Bad Renders

### Black or Missing Objects

1. Check material `visible` flag — invisible materials don't contribute to the image but still cast shadows and reflections
2. Check layer `visible` flag — if set to `false`, all materials in the layer become invisible
3. Check that the object is within the camera's field of view
4. Use the `normals` integrator to verify geometry is loaded correctly

### NaN Output (Pink/Magenta)

This usually indicates NaN values in the render. Common causes:
- Division by zero in BSDF evaluation (e.g., degenerate triangle with zero area)
- Invalid IOR values on dielectric materials
- Check that all Vec3 arrays have exactly 3 numeric values

### EXR vs PNG Output

Skewer can output both simultaneously:

```json
"render": {
  "image": {
    "outfile": "output.png",
    "exrfile": "output.exr"
  }
}
```

- **PNG**: Gamma-corrected (sRGB), 8-bit, suitable for preview. Uses Reinhard tonemapping
- **EXR**: Linear, high dynamic range, suitable for compositing. Raw radiance values
- Always render to EXR for production work; use PNG for quick previews

### Deep EXR Output

Enable deep output for compositing workflows:

```json
"render": {
  "enable_deep": true,
  "transparent_background": true
}
```

Deep EXR stores multiple depth samples per pixel, enabling correct layer compositing even when layers overlap in complex ways. The loom compositor requires deep EXR input for full deep compositing.

## See Also

- [Scene Format](scene-format.md) — Complete scene file specification
- [Compositing](compositing.md) — Layer compositing with loom
- [Animation](animation.md) — Keyframe animation and motion blur
