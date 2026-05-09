# Core Math & Transforms

The `core` directory contains the foundational math, sampling, and utility classes used throughout the renderer.

## Transforms (TRS)

Skewer avoids using 4x4 matrix multiplication for spatial transformations. Instead, it relies on a **TRS (Translation, Rotation, Scale)** structure (`skewer/src/core/math/transform.h`).

```cpp
struct TRS {
    Vec3 translation;
    Quat rotation; // Quaternions to prevent gimbal lock
    Vec3 scale;
};
```

### Why TRS instead of 4x4 Matrices?
1. **Precision & Stability**: Repeated 4x4 matrix multiplications can introduce numerical drift, especially with nested scene graphs. Quaternions are easily re-normalized (`QuatNormalize`) to remain stable.
2. **Animation**: Spherical Linear Interpolation (SLERP) is mathematically sound with Quaternions but difficult and expensive to extract from an arbitrary 4x4 matrix.
3. **Normal Transformations**: When applying non-uniform scale, normals must be transformed by the inverse-transpose of the transformation. With a TRS structure, we compute this analytically and cheaply:
   `Vec3 inv_scaled(n.x() / sx, n.y() / sy, n.z() / sz);` followed by `QuatRotate`.

## Core Utilities

- **`Vec3`**: High-performance linear algebra using standard operator overloading.
- **`Ray`**: Represents an active path segment with an origin, direction, and precomputed `inv_direction` for fast AABB intersection tests.
- **`Quat`**: Implementation of quaternions for rotation management, including `QuatFromEulerYXZ` and `QuatRotate`.
