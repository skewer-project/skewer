# Scene Format

Skewer uses JSON scene files to define renders. This document describes the format.

## Basic Structure

A scene file contains:

```json
{
  "camera": { ... },
  "objects": [ ... ],
  "materials": { ... },
  "environment": { ... }
}
```

## Camera

```json
"camera": {
  "position": [0, 0, 5],
  "target": [0, 0, 0],
  "up": [0, 1, 0],
  "fov": 45,
  "aspect": 1.778,
  "near": 0.1,
  "far": 100
}
```

## Objects

### Mesh Objects

```json
{
  "type": "obj",
  "file": "path/to/model.obj",
  "material": "material_name",
  "transform": {
    "translate": [x, y, z],
    "rotate": [rx, ry, rz],
    "scale": 1.0
  }
}
```

### Spheres

```json
{
  "type": "sphere",
  "center": [0, 0, 0],
  "radius": 1.0,
  "material": "material_name"
}
```

## Materials

### Lambertian (Diffuse)

```json
"materials": {
  "mat_name": {
    "type": "lambertian",
    "albedo": [0.8, 0.8, 0.8]
  }
}
```

### Metal

```json
"materials": {
  "mat_name": {
    "type": "metal",
    "albedo": [0.8, 0.8, 0.8],
    "roughness": 0.1
  }
}
```

### Dielectric (Glass)

```json
"materials": {
  "mat_name": {
    "type": "dielectric",
    "albedo": [1.0, 1.0, 1.0],
    "ior": 1.5,
    "roughness": 0.0
  }
}
```

## Layer System

Skewer supports a **layer-based compositing system** where scene files reference multiple layer files:

```json
{
  "camera": { ... },
  "layers": [
    "layer_camera.json",
    "layer_environment.json",
    "layer_objects.json"
  ]
}
```

Each layer is a self-contained scene fragment. The scene loader merges all layers into a single scene.

## Frame Sequences

Use `####` in filenames to render sequences:

```bash
--scene data/scenes/panda-####.json --frames 4
```

This renders `panda-0001.json`, `panda-0002.json`, etc.

## Environment

```json
"environment": {
  "type": "skybox",
  "texture": "path/to/hdr.exr"
}
```

or

```json
"environment": {
  "type": "color",
  "color": [0.1, 0.1, 0.1]
}
```

## See Also

- [Blender Tools](blender-tools.md) - Converting between Blender and Skewer formats
- [Previewer](previewer.md) - Visualizing scenes before rendering
