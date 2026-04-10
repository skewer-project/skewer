# Scene Format

Skewer uses JSON scene files to define renders. This document describes the format, including the newer layer-based system.

## Basic Structure

A scene file contains:

```json
{
  "camera": { ... },
  "context": [ ... ],
  "layers": [ ... ],
  "output_dir": "renders/"
}
```

## Camera

The camera defines the viewpoint for rendering:

```json
"camera": {
  "look_from": [0, 0, 5],
  "look_at": [0, 0, 0],
  "vup": [0, 1, 0],
  "vfov": 45,
  "aperture_radius": 0.0,
  "focus_distance": 1.0
}
```

| Field | Type | Description |
|-------|------|-------------|
| `look_from` | Vec3 | Camera position |
| `look_at` | Vec3 | Look-at target point |
| `vup` | Vec3 | Up vector (default: [0,1,0]) |
| `vfov` | float | Vertical field of view in degrees |
| `aperture_radius` | float | Lens aperture for DOF (0 = off) |
| `focus_distance` | float | Focus distance for DOF |

## Layer System

Skewer uses a **layer-based compositing system** where the main scene.json references context and layer files:

```json
{
  "camera": { ... },
  "context": ["context_camera.json", "context_lighting.json"],
  "layers": ["layer_sphere.json", "layer_objects.json"],
  "output_dir": "renders/"
}
```

### Context vs Layers

- **Context** - Static background elements (cameras, environment, lighting)
- **Layers** - Renderable objects and materials

Each referenced file is a complete layer definition with its own materials and objects.

## Layer File Format

Each layer file (context or layer) has this structure:

```json
{
  "materials": {
    "mat_name": { ... }
  },
  "objects": [
    { ... }
  ],
  "render": { ... },
  "visible": true
}
```

### Materials

Materials define surface properties. Supported types:

#### Lambertian (Diffuse)

```json
"materials": {
  "matte_red": {
    "type": "lambertian",
    "albedo": [0.8, 0.2, 0.2],
    "emission": [0, 0, 0],
    "opacity": [1, 1, 1],
    "visible": true
  }
}
```

#### Metal

```json
"materials": {
  "gold_metal": {
    "type": "metal",
    "albedo": [1.0, 0.843, 0.0],
    "roughness": 0.1,
    "emission": [0, 0, 0],
    "opacity": [1, 1, 1],
    "visible": true
  }
}
```

#### Dielectric (Glass)

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

#### Material Properties

| Property | Type | Description |
|----------|------|-------------|
| `type` | string | `lambertian`, `metal`, or `dielectric` |
| `albedo` | Vec3 | Base color (RGB, 0-1) |
| `emission` | Vec3 | Emissive color (RGB, 0+) |
| `opacity` | Vec3 | Opacity (RGB, 0-1) |
| `visible` | bool | Whether object casts shadows/reflections |
| `roughness` | float | Surface roughness (0=mirror, 1=matte) |
| `ior` | float | Index of refraction (dielectric only) |
| `albedo_texture` | string | Path to albedo texture |
| `normal_texture` | string | Path to normal map |
| `roughness_texture` | string | Path to roughness texture |

### Objects

Objects are the geometry to render:

#### Sphere

```json
{
  "type": "sphere",
  "material": "mat_name",
  "center": [0, 0, 0],
  "radius": 1.0,
  "visible": true
}
```

#### Quad (Quadrilateral)

```json
{
  "type": "quad",
  "material": "mat_name",
  "vertices": [
    [-1, -1, 0],
    [1, -1, 0],
    [1, 1, 0],
    [-1, 1, 0]
  ],
  "visible": true
}
```

#### OBJ Mesh

```json
{
  "type": "obj",
  "material": "mat_name",
  "file": "path/to/model.obj",
  "auto_fit": true,
  "visible": true,
  "transform": {
    "translate": [0, 0, 0],
    "rotate": [0, 0, 0],
    "scale": 1.0
  }
}
```

### Transform

Objects can have a transform applied:

```json
"transform": {
  "translate": [x, y, z],
  "rotate": [rx, ry, rz],
  "scale": 1.0
}
```

- `translate` - Position offset (Vec3)
- `rotate` - Rotation in degrees (Vec3)
- `scale` - Uniform scale (float) or Vec3 for non-uniform

### Render Options

Each layer can override render settings:

```json
"render": {
  "integrator": "path_trace",
  "max_samples": 200,
  "min_samples": 0,
  "max_depth": 50,
  "threads": 0,
  "tile_size": 32,
  "noise_threshold": 0.0,
  "adaptive_step": 0,
  "image": {
    "width": 1920,
    "height": 1080,
    "outfile": "output.png"
  }
}
```

## Frame Sequences

Use `####` in filenames to render sequences:

```bash
skewer-render --scene scene-####.json --frames 4
```

This renders `scene-0001.json`, `scene-0002.json`, etc.

## Example: Complete Scene

**scene.json:**
```json
{
  "camera": {
    "look_from": [0, 2, 5],
    "look_at": [0, 0, 0],
    "vfov": 60
  },
  "context": ["ctx_lighting.json"],
  "layers": ["layer_sphere.json", "layer_floor.json"],
  "output_dir": "renders/"
}
```

**ctx_lighting.json:**
```json
{
  "materials": {},
  "objects": [],
  "render": {
    "max_samples": 500
  }
}
```

**layer_sphere.json:**
```json
{
  "materials": {
    "red_gloss": {
      "type": "dielectric",
      "albedo": [1, 0.1, 0.1],
      "ior": 1.5,
      "roughness": 0.05
    }
  },
  "objects": [
    {
      "type": "sphere",
      "material": "red_gloss",
      "center": [0, 0, 0],
      "radius": 1.0
    }
  ]
}
```

## See Also

- [Blender Tools](blender-tools.md) - Converting between Blender and Skewer formats
- [Previewer](previewer.md) - Visualizing scenes before rendering
- [Scene Previewer App](../getting-started/quick-start.md#2-using-the-scene-previewer)