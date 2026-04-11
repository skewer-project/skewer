# Skewer Renderer

Skewer is a CPU-based ray-tracing renderer with deep EXR output support.

## Key Features

- **Path Tracing** - Physically-based ray tracing with importance sampling
- **Deep EXR** - Per-pixel opacity as a linear-piecewise function of depth
- **Material System** - Lambertian, Metal, Dielectric (glass with dispersion)
- **Volume Rendering** - Supporting for heterogeneous media (smoke, clouds)
- **BVH Acceleration** - Spatial data structure for fast ray-scene intersection

## Architecture

```
Scene Input (JSON)
      │
      ▼
┌─────────────┐
│Scene Loader│
└──────┬──────┘
       │
       ▼
┌─────────────┐     ┌─────────────┐
│   BVH       │────▶│  Integrator │
│ Acceleration│     │ (Path Trace)│
└─────────────┘     └──────┬──────┘
                           │
                           ▼
                   ┌─────────────┐
                   │    Film     │
                   │(Deep EXR)   │
                   └─────────────┘
```

## Rendering Pipeline

1. **Scene Load** - Parse JSON, build geometry, compile materials
2. **BVH Build** - Construct acceleration structure
3. **Camera Setup** - Configure ray origin/direction
4. **Integrate** - Trace rays, accumulate samples
5. **Film Write** - Output to PNG or Deep EXR

## Materials

| Type | Description |
|------|-------------|
| **Lambertian** | Diffuse surfaces |
| **Metal** | Metallic surfaces with adjustable roughness |
| **Dielectric** | Glass/refraction with dispersion |

### Material Parameters

- `albedo` - Surface color
- `roughness` - 0 (mirror) to 1 (matte)
- `ior` - Index of refraction
- `dispersion` - Cauchy coefficient for chromatic aberration

## Output Formats

| Format | Use Case |
|--------|----------|
| **PNG** | Quick previews, final output |
| **EXR** | Full dynamic range |
| **Deep EXR** | Compositing with transparency |

## Command Line

```bash
./skewer-render --scene <scene.json> --output <output.png> [flags]
```

| Flag | Description |
|------|-------------|
| `--scene` | Input scene JSON |
| `--output` | Output image path |
| `--samples` | Samples per pixel |
| `--threads` | Render threads |
| `--deep` | Enable deep EXR output |

## See Also

- [Scene Format](../usage/scene-format.md) - JSON scene specification
- [Architecture Overview](overview.md) - System-level architecture
- [Loom](loom.md) - Deep compositing
