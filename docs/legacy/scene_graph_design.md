# Scene Graph Design

## Overview

This document describes the design for adding animation and scene graph support to the Skewer rendering system. The scene graph enables per-object transformation animations (translation, rotation, scale) at discrete time intervals.

---

## 1. Current Architecture

### 1.1 Layer System

Skewer uses a **layer-based compositing system** where scene files reference multiple "layer" JSON files:

| File Type | Purpose |
|-----------|---------|
| `layer_*.json` | Self-contained scene fragment (objects, materials, camera) |
| `scene_XXXX.json` | Master scene that references layers |

**Example** (from `gen_altar_animation.py`):

```json
{
  "camera": {...},
  "layers": [
    "layer_camera.json",
    "layer_environment.json", 
    "layer_dodecahedron_0001.json"
  ]
}
```

The scene loader (`skewer/src/io/scene_loader.cc`) merges all layers into a single scene at load time.

### 1.2 Current Transform Format

Objects currently support static transforms only:

```json
{
  "type": "obj",
  "file": "model.obj",
  "material": "pbr_mat",
  "transform": {
    "translate": [x, y, z],
    "rotate": [rx, ry, rz],  // Euler angles in degrees
    "scale": 1.0
  }
}
```

---

## 2. Scene Graph Design

### 2.1 Core Concepts

| Concept | Description |
|---------|-------------|
| **Node** | An object in the scene with a local transform relative to its parent |
| **Channel** | Animation track for Translation, Rotation, or Scale |
| **Keyframe** | A data point at a specific time `t` |
| **Interpolation** | Math to calculate values between keyframes |

### 2.2 Scene Graph JSON Structure

```json
{
  "scene": {
    "name": "animation_name",
    "frame_rate": 24,
    "start_time": 0.0,
    "end_time": 5.0,
    "nodes": [
      {
        "id": "unique_node_id",
        "name": "friendly_name",
        "type": "obj",
        "file": "path/to/model.obj",
        "material": "material_name",
        "parent": "parent_node_id",
        "scale": 1.0,
        "channels": {
          "translation": {
            "interpolation": "linear",
            "keyframes": [
              { "t": 0.0, "value": [0, 0, 0] },
              { "t": 2.0, "value": [10, 0, 0] }
            ]
          },
          "rotation": {
            "interpolation": "slerp", 
            "keyframes": [
              { "t": 0.0, "value": [0, 0, 0, 1] },
              { "t": 2.0, "value": [0, 0.707, 0, 0.707] }
            ]
          },
          "scale": {
            "interpolation": "step",
            "keyframes": [
              { "t": 0.0, "value": 1.0 },
              { "t": 3.0, "value": 1.5 }
            ]
          }
        }
      }
    ],
    "static_objects": [...],
    "materials": {...},
    "render": {...}
  }
}
```

### 2.3 Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Flat list with parent IDs** | Simple hierarchy; no deep nesting needed |
| **Quaternions for rotation** | Avoids gimbal lock; enables SLERP interpolation |
| **Quaternion format `[x, y, z, w]`** | Standard math notation |
| **Sparse keyframes** | Only store animation keyframes, not every frame |
| **Interpolation per channel** | Flexibility (linear for position, step for scale) |
| **Time in seconds** | Aligns with render engine math; frame rate converts at submit time |
| **Camera as node** | Enables camera flythrough animations |
| **No channels = static** | Static objects don't need keyframes; evaluator returns base transform |

### 2.4 Interpolation Methods

| Method | Use Case | Math |
|--------|----------|------|
| `linear` | Translation, Scale | Standard linear interpolation (LERP) |
| `step` | On/off values | Hold previous value until next keyframe |
| `slerp` | Rotation | Spherical linear interpolation (quaternions only) |

---

## 3. How Layers and Scene Graph Work Together

The layer system and scene graph serve **different purposes**:

| Concept | Purpose |
|---------|---------|
| **Layer** | Render pipeline composition (background, character, FX) |
| **Scene Graph** | Object-level animation (walk cycle, camera movement) |

### Combined Example

```json
{
  "scene": {
    "layers": [
      "layer_camera.json",
      "layer_environment.json",
      "layer_character.json"
    ],
    "nodes": [
      {
        "id": "character",
        "type": "obj",
        "file": "character.obj",
        "parent": null,
        "channels": {
          "translation": {
            "interpolation": "linear",
            "keyframes": [
              { "t": 0.0, "value": [0, 0, 0] },
              { "t": 5.0, "value": [10, 0, 0] }
            ]
          }
        }
      }
    ],
    "render": {...}
  }
}
```

---

## 4. Implementation Plan

### Phase 1: Core Math (C++)

| Task | Files |
|------|-------|
| Add quaternion struct and math | `skewer/src/core/math/quaternion.h` |
| Implement SLERP interpolation | `skewer/src/core/math/quaternion.h` |
| Add LERP for Vec3 | `skewer/src/core/math/utils.h` |

### Phase 2: Scene Loader Updates

| Task | Files |
|------|-------|
| Parse animation channels | `skewer/src/io/scene_loader.cc` |
| Add node hierarchy parsing | `skewer/src/io/scene_loader.cc` |

### Phase 3: Animation Evaluator

| Task | Files |
|------|-------|
| Create evaluator class | `skewer/src/scene/animation_evaluator.{h,cc}` |
| Implement keyframe lookup | `skewer/src/scene/animation_evaluator.cc` |
| Implement LERP/SLERP | `skewer/src/scene/animation_evaluator.cc` |

### Phase 4: Render Integration

| Task | Files |
|------|-------|
| Add time parameter to render session | `skewer/src/session/render_session.{h,cc}` |
| Evaluate animations before each frame | `skewer/src/session/render_session.cc` |
| Update transform application | `skewer/src/core/math/transform.h` |

### Phase 5: Blender Export

| Task | Files |
|------|-------|
| Extract animation tracks from Blender | `tools/blender_to_skewer/blender_export.py` |
| Convert to scene graph format | `tools/blender_to_skewer/blender_export.py` |
| Convert Euler → Quaternion | `tools/blender_to_skewer/blender_export.py` |

---

## 5. JSON Schema (Reference)

### Scene Root

```json
{
  "scene": {
    "name": "string",
    "frame_rate": "number (default: 24)",
    "start_time": "number (seconds)",
    "end_time": "number (seconds)",
    "nodes": [...],
    "static_objects": [...],
    "materials": {...},
    "render": {...}
  }
}
```

### Node

```json
{
  "id": "string (unique)",
  "name": "string",
  "type": "obj | sphere | quad | camera",
  "file": "string (relative path, for type=obj)",
  "material": "string",
  "parent": "string | null",
  "scale": "number",
  "channels": {
    "translation": {
      "interpolation": "linear | step",
      "keyframes": [{ "t": "number", "value": "[x,y,z]" }]
    },
    "rotation": {
      "interpolation": "linear | slerp | step",
      "keyframes": [{ "t": "number", "value": "[x,y,z,w]" }]
    },
    "scale": {
      "interpolation": "linear | step", 
      "keyframes": [{ "t": "number", "value": "number" }]
    }
  }
}
```

### Keyframe

```json
{
  "t": "number (time in seconds)",
  "value": "array | number (depends on channel type)"
}
```

---

## 6. Backward Compatibility

The new scene graph format maintains backward compatibility with existing static scenes:

| Format | How It's Handled |
|--------|-----------------|
| Static (`objects` array) | Still fully supported; converted to flat node list at load |
| Animated (`scene.nodes`) | New format with full animation support |
| Hybrid | Both can coexist; static_objects + nodes in same file |

### Static Objects vs Animated Objects

- **Static objects**: No `channels` property. Evaluator returns the base transform.
- **Animated objects**: Have `channels` property with `keyframes` array.

This design is:
- **Easier for implementation**: Single code path - evaluator always checks for channels
- **More efficient**: No unnecessary keyframe storage for static objects

---

## 7. Example: Full Animation Scene

### Panda Walk Animation (2 seconds)

```json
{
  "scene": {
    "name": "panda_walk",
    "frame_rate": 24,
    "start_time": 0.0,
    "end_time": 2.0,
    "nodes": [
      {
        "id": "panda",
        "name": "Panda",
        "type": "obj",
        "file": "../objects/Po/Po.obj",
        "material": "panda_mat",
        "parent": null,
        "scale": 1.0,
        "channels": {
          "translation": {
            "interpolation": "linear",
            "keyframes": [
              { "t": 0.0, "value": [0, 0, 0] },
              { "t": 1.0, "value": [2, 0, 0] },
              { "t": 2.0, "value": [4, 0, 0] }
            ]
          },
          "rotation": {
            "interpolation": "slerp",
            "keyframes": [
              { "t": 0.0, "value": [0, 0, 0, 1] },
              { "t": 1.0, "value": [0, 0.707, 0, 0.707] },
              { "t": 2.0, "value": [0, 0, 0, 1] }
            ]
          }
        }
      },
      {
        "id": "camera_main",
        "name": "Camera",
        "type": "camera",
        "parent": null,
        "channels": {
          "translation": {
            "interpolation": "linear",
            "keyframes": [
              { "t": 0.0, "value": [1.6, 1, 3.6] },
              { "t": 2.0, "value": [0, 1.5, 4] }
            ]
          }
        }
      }
    ],
    "static_objects": [
      {
        "type": "quad",
        "material": "floor",
        "vertices": [[-50,-1,-50], [50,-1,-50], [50,-1,50], [-50,-1,50]]
      }
    ],
    "materials": {
      "panda_mat": {
        "type": "lambertian",
        "albedo": [0.8, 0.8, 0.8]
      },
      "floor": {
        "type": "lambertian", 
        "albedo": [0.5, 0.5, 0.5]
      }
    },
    "camera": {
      "look_from": [0, 1, 5],
      "look_at": [0, 0, 0],
      "vfov": 40
    },
    "render": {
      "integrator": "path_trace",
      "samples_per_pixel": 100,
      "image": { "width": 800, "height": 450 }
    }
  }
}
```

---

## 8. Animation Design Philosophy

### One Fluid Animation

All transformation keyframes for an object are contained within a single animation. This means:

1. **Single `channels` object per node**: All translation, rotation, and scale animations are defined together
2. **Unified timeline**: All keyframes reference the same time `t` space
3. **Complete motion**: The keyframe sequence describes the entire motion from `start_time` to `end_time`

### Why Not Separate Clips?

Rather than having separate animation clips (walk, run, idle) that get blended:

- **Simplicity**: One source of truth per animation
- **Blender compatibility**: Aligns with how Blender exports baked animations
- **Flexibility**: Artists can create complex multi-phase animations in Blender and export as one complete sequence

If blending is needed later, it can be implemented at a higher level by:
1. Loading multiple scene files with different `scene.nodes` configurations
2. Having a separate "animation composition" layer that blends between them

---

*Document Version: 1.0*  
*Last Updated: April 2026*
