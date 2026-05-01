# Scene Format

Skewer uses a layer-based JSON scene system. A scene consists of a **manifest** (`scene.json`) that references **context** and **layer** files, each defining geometry, materials, and render settings.

## Overview

```
data/scenes/my_scene/
├── scene.json          # Manifest: camera, context/layer refs, output_dir
├── ctx.json            # Context: static scene-wide materials + objects
├── layer_bg.json       # Layer: background objects
└── layer_fg.json       # Layer: foreground objects
```

The manifest defines the camera and lists which files to load. Context files provide scene-wide shared content (lighting, sky, environment). Layer files define the objects to render, each with their own materials and geometry.

## scene.json (Manifest)

The top-level file that orchestrates a render:

```json
{
  "camera": { ... },
  "context": ["ctx.json"],
  "layers": ["layer_bg.json", "layer_fg.json"],
  "output_dir": "renders/"
}
```

!!! tip "Quick Start"
    **Quick Start:** Use the sample scene template at `apps/scene-previewer/public/templates/scene.json` as a starting point. It includes a camera, context layer, and three object layers — just copy it into your scene directory and customize the values.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `camera` | object | Yes | Camera configuration |
| `context` | string[] | No | Paths to context layer files |
| `layers` | string[] | Yes | Paths to render layer files (back-to-front order) |
| `output_dir` | string | No | Output directory (local path or `gs://` URI). Empty = cwd |

### Camera

```json
"camera": {
  "look_from": [0, 2, 5],
  "look_at": [0, 0, 0],
  "vup": [0, 1, 0],
  "vfov": 60,
  "aperture_radius": 0.05,
  "focus_distance": 5.0,
  "shutter_open": 0.0,
  "shutter_close": 0.1
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `look_from` | Vec3 | — | Camera position in world space |
| `look_at` | Vec3 | — | Point the camera looks toward |
| `vup` | Vec3 | `[0, 1, 0]` | Up vector for camera orientation |
| `vfov` | float | `90` | Vertical field of view in degrees |
| `aperture_radius` | float | `0` | Lens aperture radius. `0` = no depth of field; >0 enables DOF with `focus_distance` |
| `focus_distance` | float | `1.0` | Distance from camera that is in focus (meaningful when `aperture_radius > 0`) |
| `shutter_open` | float | `0` | Time when shutter opens (animation time units). Non-zero enables motion blur |
| `shutter_close` | float | `0` | Time when shutter closes. If equal to `shutter_open`, no motion blur |

**Depth of Field:** Set `aperture_radius > 0` and `focus_distance` to the distance of your subject. Objects at `focus_distance` are sharp; objects closer or farther are progressively blurred.

**Motion Blur:** Set `shutter_open` and `shutter_close` to different values (e.g., `0.0` and `0.1`). Animated objects will blur according to their movement during this interval. The shutter interval must match the time range of your animation keyframes for correct blur.

## Layer/Context Files

Both context and layer files share the same structure:

```json
{
  "materials": { ... },
  "media": { ... },
  "graph": [ ... ],
  "render": { ... },
  "visible": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `materials` | object | Named material definitions |
| `media` | object | Named volume medium definitions (NanoVDB) |
| `graph` | array | Scene graph — hierarchical tree of geometry nodes |
| `render` | object | Render options override (optional) |
| `visible` | bool | Layer-level visibility. If `false`, all materials become invisible but still cast shadows/reflections |

## Materials

Materials are defined by name in the `materials` object. Each material has a `type` that determines its optical behavior.

### Lambertian (Diffuse)

```json
"materials": {
  "matte_red": {
    "type": "lambertian",
    "albedo": [0.8, 0.2, 0.2],
    "emission": [0, 0, 0],
    "opacity": [1, 1, 1],
    "visible": true,
    "albedo_texture": "textures/brick_albedo.png",
    "normal_texture": "textures/brick_normal.png"
  }
}
```

### Metal

```json
"materials": {
  "gold": {
    "type": "metal",
    "albedo": [1.0, 0.843, 0.0],
    "roughness": 0.1,
    "emission": [0, 0, 0],
    "opacity": [1, 1, 1],
    "visible": true,
    "roughness_texture": "textures/brushed_metal_rough.png"
  }
}
```

### Dielectric (Glass / Transparent)

```json
"materials": {
  "glass": {
    "type": "dielectric",
    "albedo": [1.0, 1.0, 1.0],
    "ior": 1.5,
    "roughness": 0.0,
    "emission": [0, 0, 0],
    "opacity": [1, 1, 1],
    "visible": true
  }
}
```

### Material Properties

| Property | Type | Default | Lambertian | Metal | Dielectric | Description |
|----------|------|---------|:----------:|:-----:|:----------:|-------------|
| `type` | string | — | ✓ | ✓ | ✓ | `lambertian`, `metal`, or `dielectric` |
| `albedo` | Vec3 | `[1,1,1]` | ✓ | ✓ | ✓ | Base color (RGB, 0-1) |
| `emission` | Vec3 | `[0,0,0]` | ✓ | ✓ | ✓ | Emissive color (RGB, 0+). Use values >1 for HDR light sources |
| `opacity` | Vec3 | `[1,1,1]` | ✓ | ✓ | ✓ | Per-channel opacity (RGB, 0-1) |
| `visible` | bool | `true` | ✓ | ✓ | ✓ | Whether the material contributes to the image. Invisible materials still cast shadows and appear in reflections |
| `roughness` | float | `0` | — | ✓ | ✓ | Microfacet roughness (0=mirror, 1=matte) |
| `ior` | float | — | — | — | ✓ | Index of refraction (1.5=glass, 1.33=water, 2.42=diamond) |
| `albedo_texture` | string | — | ✓ | ✓ | ✓ | Path to albedo/color texture map |
| `normal_texture` | string | — | ✓ | ✓ | ✓ | Path to normal map |
| `roughness_texture` | string | — | — | ✓ | ✓ | Path to roughness map |

**Textures** are resolved relative to the layer file's directory.

**Emission** makes a material a light source. Higher RGB values produce brighter light. Combine with a `lambertian` material and `albedo: [0,0,0]` for pure emissive lights (like the sun).

## Scene Graph

The `graph` array defines a hierarchical tree of geometry. Each entry is a node that can contain children, forming a scene graph where transforms are inherited down the tree.

### Node Types

#### Group

A container node that groups children and applies a transform to all of them:

```json
{
  "name": "earth_orbit",
  "transform": {
    "keyframes": [
      { "time": 0, "rotate": [0, 0, 0] },
      { "time": 1, "rotate": [0, 360, 0] }
    ]
  },
  "children": [
    {
      "type": "obj",
      "file": "Earth.obj",
      "name": "earth_body",
      "transform": {
        "translate": [42.5, 0, 0],
        "scale": 4
      }
    }
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Optional identifier for the node |
| `transform` | object | Static or animated transform (applied to all children) |
| `children` | array | Child nodes (groups, spheres, quads, or objs) |

#### Sphere

```json
{
  "type": "sphere",
  "material": "glass",
  "center": [0, 1, 0],
  "radius": 0.5,
  "visible": true,
  "inside_medium": "fog",
  "outside_medium": "air"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | string | Yes | Must be `"sphere"` |
| `material` | string | Yes | Material name (or `"null"`/`"none"` for no material) |
| `center` | Vec3 | Yes | Center position |
| `radius` | float | Yes | Radius |
| `visible` | bool | No | Per-object visibility override |
| `inside_medium` | string | No | Volume medium inside the sphere |
| `outside_medium` | string | No | Volume medium outside the sphere |

#### Quad

A four-vertex planar surface (useful for mirrors, light panels, floors):

```json
{
  "type": "quad",
  "material": "mirror",
  "vertices": [
    [-2, 0, -2],
    [ 2, 0, -2],
    [ 2, 0,  2],
    [-2, 0,  2]
  ],
  "visible": true
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | string | Yes | Must be `"quad"` |
| `material` | string | Yes | Material name |
| `vertices` | Vec3[4] | Yes | Four corner vertices in counter-clockwise order |
| `visible` | bool | No | Per-object visibility override |

#### OBJ Mesh

Loads a Wavefront `.obj` file:

```json
{
  "type": "obj",
  "file": "models/teapot.obj",
  "material": "ceramic",
  "name": "teapot",
  "auto_fit": true,
  "visible": true,
  "transform": {
    "translate": [0, 0, 0],
    "rotate": [0, 45, 0],
    "scale": 2.0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | string | Yes | Must be `"obj"` |
| `file` | string | Yes | Path to `.obj` file (relative to scene directory) |
| `material` | string | No | Overrides all mesh materials with this one |
| `name` | string | No | Node identifier |
| `auto_fit` | bool | No (default `true`) | Auto-scale and center the mesh to fit a unit bounding box |
| `visible` | bool | No | Per-object visibility override |
| `transform` | object | No | Static or animated transform |

## Transforms

Transforms can be **static** (single TRS values) or **animated** (keyframes with interpolation).

### Static Transform

```json
"transform": {
  "translate": [1, 2, 3],
  "rotate": [0, 45, 0],
  "scale": 2.0
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `translate` | Vec3 | `[0, 0, 0]` | Position offset |
| `rotate` | Vec3 | `[0, 0, 0]` | Rotation in degrees, applied as Euler angles (X, Y, Z) |
| `scale` | float or Vec3 | `1` | Uniform scale (float) or per-axis scale (Vec3) |

### Animated Transform (Keyframes)

```json
"transform": {
  "keyframes": [
    { "time": 0, "translate": [0, 0, 0], "rotate": [0, 0, 0] },
    { "time": 0.5, "translate": [5, 0, 0], "rotate": [0, 180, 0], "curve": "ease-in-out" },
    { "time": 1, "translate": [0, 0, 0], "rotate": [0, 360, 0] }
  ]
}
```

Each keyframe has:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `time` | float | Yes | Time value (animation time units) |
| `translate` | Vec3 | No | Position at this keyframe. Accumulates from previous keyframe if omitted |
| `rotate` | Vec3 | No | Rotation at this keyframe (degrees). Accumulates from previous keyframe if omitted |
| `scale` | float or Vec3 | No | Scale at this keyframe. Accumulates from previous keyframe if omitted |
| `curve` | string or object | No (default `"linear"`) | Interpolation curve from this keyframe to the next |

**TRS accumulation:** When a keyframe omits a field, it inherits and continues from the previous keyframe's accumulated value. This means `rotate: [0, 360, 0]` at time 1 after `rotate: [0, 180, 0]` at time 0.5 produces continuous rotation, not a snap back.

### Interpolation Curves

The `curve` field controls easing between keyframes:

| Preset | Description |
|--------|-------------|
| `"linear"` | Constant speed (default) |
| `"ease-in"` | Slow start, fast end |
| `"ease-out"` | Fast start, slow end |
| `"ease-in-out"` | Slow start and end, fast middle |

#### Custom Bezier Curve

For fine-grained control, use a cubic Bezier with 4 control point values:

```json
"curve": { "bezier": [0.25, 0.1, 0.25, 1.0] }
```

The array is `[p1x, p1y, p2x, p2y]` where P0=(0,0) and P3=(1,1) are fixed. This matches CSS `cubic-bezier()` syntax.

### Transform Inheritance

Transforms compose down the scene graph. A child node's transform is applied after its parent's, so:

```
world_position = parent_transform × child_transform × local_position
```

Animated group transforms create orbital motion: animate the group's rotation, and all children orbit around the group's origin.

## Media (Volumes)

NanoVDB volumes add participating media (fog, smoke, clouds) to the scene:

```json
"media": {
  "fog": {
    "type": "nanovdb",
    "file": "volumes/cloud.vdb",
    "sigma_a": [0.01, 0.01, 0.01],
    "sigma_s": [0.9, 0.9, 0.9],
    "g": 0.0,
    "density_multiplier": 1.0,
    "scale": 1.0,
    "translate": [0, 0, 0]
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `type` | string | — | Must be `"nanovdb"` |
| `file` | string | — | Path to `.vdb` file |
| `sigma_a` | Vec3 | `[0,0,0]` | Absorption coefficient (RGB) |
| `sigma_s` | Vec3 | `[0,0,0]` | Scattering coefficient (RGB) |
| `g` | float | `0` | Henyey-Greenstein phase function parameter (-1 to 1, 0=isotropic) |
| `density_multiplier` | float | `1.0` | Scales all density values from the VDB |
| `scale` | float | `1.0` | Spatial scale factor |
| `translate` | Vec3 | `[0,0,0]` | Spatial offset |

Use `inside_medium` / `outside_medium` on spheres to attach media to geometry:

```json
{
  "type": "sphere",
  "material": "glass",
  "center": [0, 0, 0],
  "radius": 5.0,
  "inside_medium": "fog"
}
```

## Render Options

Each layer file can override render settings. The highest-priority render config wins (later layers override earlier ones):

```json
"render": {
  "integrator": "path_trace",
  "max_samples": 1024,
  "min_samples": 16,
  "max_depth": 5,
  "threads": 0,
  "tile_size": 32,
  "noise_threshold": 0.05,
  "adaptive_step": 16,
  "enable_deep": false,
  "transparent_background": false,
  "visibility_depth": 1,
  "save_sample_map": false,
  "image": {
    "width": 1920,
    "height": 1080,
    "outfile": "output.png",
    "exrfile": "output.exr"
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `integrator` | string | `"path_trace"` | `"path_trace"` for standard rendering, `"normals"` for normal visualization |
| `max_samples` | int | `200` | Maximum samples per pixel |
| `min_samples` | int | `1` | Minimum samples before adaptive convergence checks begin |
| `max_depth` | int | `50` | Maximum ray bounce depth |
| `threads` | int | `0` | Number of render threads. `0` = auto-detect (all available cores) |
| `tile_size` | int | `32` | Tile dimension for work-stealing parallelism (NxN pixels) |
| `noise_threshold` | float | `0` | Adaptive sampling convergence threshold. `0` = disabled (always render to `max_samples`) |
| `adaptive_step` | int | `16` | Samples between convergence checks when adaptive sampling is enabled |
| `enable_deep` | bool | `false` | Enable deep pixel buffers (for compositing) |
| `transparent_background` | bool | `false` | Missed primary rays produce alpha=0 instead of black. Required for clean layer compositing |
| `visibility_depth` | int | `1` | How many surface bounces to check for "covered" pixels when `transparent_background=true`. `1` = only direct camera visibility; higher values allow seeing visible objects through invisible surfaces |
| `save_sample_map` | bool | `false` | Write per-pixel sample count heatmap (debug) |
| `image.width` | int | `800` | Output image width in pixels |
| `image.height` | int | `450` | Output image height in pixels |
| `image.outfile` | string | `"output.png"` | Output PNG filename |
| `image.exrfile` | string | `"output.exr"` | Output EXR filename (HDR) |

### Adaptive Sampling

Set `noise_threshold > 0` to enable adaptive sampling. Pixels that converge below the noise threshold stop sampling early, saving render time on simple regions while complex areas (edges, shadows, reflections) continue to `max_samples`.

```json
"render": {
  "max_samples": 4096,
  "min_samples": 64,
  "noise_threshold": 0.02,
  "adaptive_step": 32
}
```

This renders up to 4096 samples but stops per-pixel when noise drops below 0.02, checking every 32 samples after the first 64.

### Transparent Backgrounds

For multi-layer compositing, set `transparent_background: true`:

```json
"render": {
  "transparent_background": true,
  "visibility_depth": 2
}
```

- Rays that miss all geometry produce alpha=0 (transparent) instead of black
- `visibility_depth` controls how many bounces are checked:
  - `1` (default): only objects directly visible from the camera count
  - `2-4`: allows visible objects seen through invisible surfaces (e.g., a visible sphere reflected in an invisible mirror)

## Complete Example

A scene with an animated solar system:

**scene.json:**
```json
{
  "camera": {
    "look_from": [0, 80, 120],
    "look_at": [0, 0, 0],
    "vfov": 45,
    "aperture_radius": 0,
    "focus_distance": 100
  },
  "context": ["ctx.json"],
  "layers": ["layer_earth.json", "layer_mars.json"],
  "output_dir": "renders/solar_system/"
}
```

**ctx.json:**
```json
{
  "materials": {
    "sun_mat": {
      "type": "lambertian",
      "albedo": [0, 0, 0],
      "emission": [2, 1.4, 0.6],
      "opacity": [1, 1, 1],
      "visible": true
    },
    "star": {
      "type": "lambertian",
      "albedo": [0, 0, 0],
      "emission": [20, 20, 20],
      "opacity": [1, 1, 1],
      "visible": true
    }
  },
  "graph": [
    {
      "type": "obj",
      "file": "Sun.obj",
      "material": "sun_mat",
      "transform": { "scale": 13.5 }
    },
    {
      "type": "sphere",
      "material": "star",
      "center": [-100, 50, -200],
      "radius": 0.3
    }
  ]
}
```

**layer_earth.json:**
```json
{
  "materials": {
    "earth_surf": {
      "type": "lambertian",
      "albedo": [0.2, 0.4, 0.8],
      "emission": [0, 0, 0],
      "opacity": [1, 1, 1],
      "visible": true
    }
  },
  "graph": [
    {
      "name": "earth_orbit",
      "transform": {
        "keyframes": [
          { "time": 0, "rotate": [0, 0, 0] },
          { "time": 1, "rotate": [0, 360, 0] }
        ]
      },
      "children": [
        {
          "type": "obj",
          "file": "Earth.obj",
          "material": "earth_surf",
          "transform": {
            "translate": [42, 0, 0],
            "scale": 4
          }
        }
      ]
    }
  ],
  "render": {
    "integrator": "path_trace",
    "max_samples": 1024,
    "max_depth": 4,
    "image": {
      "width": 1920,
      "height": 1080
    }
  }
}
```

## Frame Sequences

Render a sequence of scene files using `####` as a frame number placeholder:

```bash
skewer-render --scene scene-####.json --frames 4
```

This renders `scene-0001.json`, `scene-0002.json`, `scene-0003.json`, `scene-0004.json`.

## See Also

- [Rendering Tips](rendering-tips.md) - Best practices for quality and performance
- [Animation](animation.md) - Keyframe animation and motion blur
- [Compositing](compositing.md) - Layer compositing with loom
- [Blender Tools](blender-tools.md) - Converting between Blender and Skewer formats
- [Previewer](previewer.md) - Visualizing scenes before rendering
- [CLI Reference](cli.md) - Command-line options
- [GCP Deployment](../deployment/gcp.md) - Cloud rendering setup
