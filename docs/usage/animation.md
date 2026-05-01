# Animation

Skewer supports keyframe animation for any transformable element in the scene graph. This guide covers the animation system, motion blur, and how to create animated renders.

## Overview

Animation in Skewer is defined through **keyframed transforms** on scene graph nodes. Each keyframe specifies a time and a transform (translation, rotation, scale), and Skewer interpolates between them during rendering.

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
      "transform": { "translate": [42.5, 0, 0], "scale": 4 }
    }
  ]
}
```

In this example, a group node rotates 360 degrees around the Y-axis from time 0 to time 1, carrying its child (the Earth model) in an orbital path.

## Keyframe Structure

Each keyframe in the `keyframes` array has:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `time` | float | Yes | Time value in arbitrary animation units |
| `translate` | Vec3 | No | Position `[x, y, z]`. Omitted fields accumulate from previous keyframe |
| `rotate` | Vec3 | No | Rotation in degrees `[rx, ry, rz]` (Euler angles). Accumulates from previous |
| `scale` | float or Vec3 | No | Uniform scale (number) or per-axis scale `[sx, sy, sz]`. Accumulates from previous |
| `curve` | string or object | No (default `"linear"`) | Interpolation curve to the next keyframe |

### TRS Accumulation

Keyframes use **accumulative interpolation**: when a field is omitted from a keyframe, it retains the value from the previous keyframe (or the default for the first keyframe — translate `[0,0,0]`, rotate `[0,0,0]`, scale `[1,1,1]`).

```json
"keyframes": [
  { "time": 0, "translate": [0, 0, 0] },
  { "time": 1, "translate": [5, 0, 0] },
  { "time": 2 }
]
```

- At time 0: translate = `[0, 0, 0]`
- At time 1: translate = `[5, 0, 0]`
- At time 2: translate = `[5, 0, 0]` (accumulated from time 1, since omitted)

This means the object moves from origin to `[5, 0, 0]` in the first second, then stays there.

### Rotation Accumulation

Rotation values **accumulate**, which enables continuous spinning:

```json
"keyframes": [
  { "time": 0, "rotate": [0, 0, 0] },
  { "time": 0.5, "rotate": [0, 180, 0] },
  { "time": 1, "rotate": [0, 360, 0] },
  { "time": 1.5, "rotate": [0, 540, 0] },
  { "time": 2, "rotate": [0, 720, 0] }
]
```

The object completes two full rotations over 2 time units. The rotation is interpolated via **spherical linear interpolation (SLERP)** on quaternions, which avoids gimbal lock and produces smooth rotation.

### Clamping

When the animation time falls outside the keyframe range, the transform is clamped to the nearest endpoint:

- `t <= first_keyframe.time` → returns first keyframe's transform
- `t >= last_keyframe.time` → returns last keyframe's transform

There is **no automatic looping or ping-ponging** — you must design keyframes to cover the full animation duration.

## Interpolation Curves

The `curve` field on each keyframe controls how the interpolation eases between that keyframe and the next.

### Preset Curves

| Preset | Description | Visual |
|--------|-------------|--------|
| `"linear"` | Constant speed (default) | Straight line |
| `"ease-in"` | Slow start, fast end | Accelerating |
| `"ease-out"` | Fast start, slow end | Decelerating |
| `"ease-in-out"` | Slow start and end, fast middle | S-curve |

### Custom Bezier Curves

For fine-grained control, specify a cubic Bezier curve:

```json
"curve": { "bezier": [0.25, 0.1, 0.25, 1.0] }
```

The four values are `[p1x, p1y, p2x, p2y]` — the control points of a cubic Bezier where P0=(0,0) and P3=(1,1) are fixed. This is the same format as CSS `cubic-bezier()`.

The curve maps a normalized time parameter `u` in `[0, 1]` to an eased value in `[0, 1]`. Skewer uses Newton's method to solve for the Bezier parameter `t` given `u`, then evaluates the Y component.

### Curve Assignment

The `curve` on a keyframe governs the interpolation **from that keyframe to the next**. The curve on the last keyframe is ignored (there is no next keyframe to interpolate toward).

## Scene Graph Transforms

### Static Transforms

A transform without keyframes is a single static TRS:

```json
"transform": {
  "translate": [1, 2, 3],
  "rotate": [0, 45, 0],
  "scale": 2.0
}
```

### Transform Inheritance

Transforms compose down the scene graph. A child node's world transform is the composition of all ancestor transforms with its own:

```
world_transform = parent_transform × grandparent_transform × ... × own_transform
```

The composition follows standard TRS math:
- **Scale**: `parent_scale × child_scale` (component-wise)
- **Rotation**: `parent_rotation × child_rotation` (quaternion multiplication)
- **Translation**: `parent_translation + parent_rotation × (parent_scale × child_translation)`

### Animated Groups

When a group node has an animated transform, all children inherit the animated transform at each ray time:

```
world_position(t) = group_transform(t) × child_transform × local_position
```

This is the standard pattern for orbital animation: animate the group's rotation, and children orbit around the group's origin.

## Motion Blur

Motion blur is produced by sampling rays at random times within the camera's shutter interval:

```json
"camera": {
  "shutter_open": 0.0,
  "shutter_close": 0.1
}
```

### How It Works

1. Each ray sample gets a random time: `ray_time = shutter_open + random() × (shutter_close - shutter_open)`
2. Animated transforms are evaluated at `ray_time` via `AnimatedTransform::Evaluate(ray_time)`
3. Bounding volumes are expanded to cover the full shutter interval for acceleration structure correctness
4. The accumulation of samples at different times produces motion blur

### Matching Shutter to Animation

The shutter interval determines how much motion is captured:

```
If animation runs from time 0 to time 1:
  shutter [0, 0.1] → captures 10% of total motion (light blur)
  shutter [0, 0.5] → captures 50% of total motion (medium blur)
  shutter [0, 1.0] → captures full motion (heavy blur)
```

!!! tip "Shutter Interval"
    For a specific motion blur amount, set `shutter_close - shutter_open` to the fraction of the animation you want blurred. A 10% blur on a 1-second animation uses `[0, 0.1]`.

### Shutter and Frame Sequences

When rendering frame sequences (`####` placeholder), each frame loads a **separate scene file** with its own independent animation timeline. The shutter interval is per-scene-file:

```json
// scene-0001.json
"camera": { "shutter_open": 0.0, "shutter_close": 0.1 }

// scene-0002.json
"camera": { "shutter_open": 0.0, "shutter_close": 0.1 }
```

If you want motion blur to progress across frames, you must either:
1. Use different shutter intervals in each scene file, or
2. Use a single scene file with keyframes that span all frame times and advance the shutter interval per frame

### Performance Impact

Motion blur significantly increases noise because each sample effectively renders a different frame of the animation. Expect to need **2-4× more samples** than a static scene for comparable quality.

### Bounding Volume Expansion

Animated objects require expanded bounding volumes for the BVH acceleration structure. The bounding box is computed as the union of the object's bounds at `shutter_open` and `shutter_close`. This can make the BVH less efficient for fast-moving objects, slightly increasing ray intersection cost.

## Frame Sequences

To render an animation as a sequence of still frames, use the `####` placeholder:

```bash
skewer-render --scene scene-####.json --frames 10
```

This renders `scene-0001.json` through `scene-0010.json`. Each file is a complete, independent scene with its own:
- Camera (including shutter settings)
- Context and layer references
- Keyframe time values

### Creating Frame Sequences

There are two common patterns:

**Pattern 1: Separate files per frame.** Each `scene-NNNN.json` has keyframes at times appropriate for that frame. This gives maximum flexibility but requires generating many files.

**Pattern 2: Single timeline.** One scene file with keyframes spanning the full animation duration (e.g., `time: 0` to `time: 10`). Each frame uses the same file but with different `shutter_open`/`shutter_close` values to capture different portions of the motion. This requires a script to generate per-frame scene files with adjusted shutter intervals.

## Acceleration Structure for Animation

Skewer uses a two-level BVH:

- **Bottom-level BVH**: Static mesh geometry, built once per layer load
- **Top-level BVH (TLAS)**: Instance transforms, rebuilt per ray time

For animated instances, the TLAS evaluates transforms at the ray's time. For static instances, the transform is precomputed at `t=0`.

### Static vs Animated Instances

An instance is classified as animated if any node in its transform chain has more than one keyframe. Static instances use a single precomputed transform, which is faster for ray traversal. If only part of a scene is animated, ensure the animated nodes are in separate groups to minimize the number of animated instances.

## Common Patterns

### Orbital Motion

```json
{
  "name": "orbit_group",
  "transform": {
    "keyframes": [
      { "time": 0, "rotate": [0, 0, 0] },
      { "time": 1, "rotate": [0, 360, 0] }
    ]
  },
  "children": [
    {
      "type": "obj",
      "file": "planet.obj",
      "transform": { "translate": [10, 0, 0] }
    }
  ]
}
```

### Linear Translation

```json
{
  "name": "moving_camera_rig",
  "transform": {
    "keyframes": [
      { "time": 0, "translate": [-5, 2, 0] },
      { "time": 1, "translate": [5, 2, 0], "curve": "ease-in-out" }
    ]
  },
  "children": [ ... ]
}
```

### Pulsing Scale

```json
{
  "transform": {
    "keyframes": [
      { "time": 0, "scale": 1.0 },
      { "time": 0.5, "scale": 1.2, "curve": "ease-out" },
      { "time": 1, "scale": 1.0, "curve": "ease-in" }
    ]
  }
}
```

## See Also

- [Scene Format](scene-format.md) — Transform and keyframe syntax reference
- [Rendering Tips](rendering-tips.md) — Motion blur quality and performance tips
- [Compositing](compositing.md) — Layer compositing with loom
