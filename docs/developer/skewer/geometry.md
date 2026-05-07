# Geometry

This section details the geometric primitives and intersection logic supported by Skewer.

## Ray-Triangle Intersection (Möller–Trumbore)

Skewer implements an optimized **Möller–Trumbore** algorithm (`skewer/src/geometry/intersect_triangle.h`) for all mesh geometry.

### Pre-baked Triangle Data
To maximize cache coherency, Skewer avoids index/vertex buffer indirection during the trace. Meshes are "baked" into flat `Triangle` structs:

```cpp
struct Triangle {
    Vec3 p0;
    Vec3 e1, e2; // Edges: (p1-p0) and (p2-p0)
    Vec3 n0, n1, n2; // Vertex normals
    UV uv0, uv1, uv2; // Texture coordinates
    // ... material and medium info
};
```
By storing edges `e1` and `e2` directly, the integrator saves two vector subtractions per intersection test.

### Shading & Normal Interpolation
The algorithm calculates $u$ and $v$ barycentric coordinates, which are used to interpolate shading normals and UVs:
- **Shading Normal**: $n_s = \text{Normalize}(w \cdot n_0 + u \cdot n_1 + v \cdot n_2)$, where $w = 1 - u - v$.
- **Tangent Frame**: If normal mapping is required, Skewer computes the tangent frame (`dpdu`, `dpdv`) using the UV deltas. It includes a fallback to an arbitrary orthogonal frame for degenerate UVs to prevent `NaN` results.

## Ray-AABB Intersection (Slab Method)

Bounding box intersections are critical for BVH traversal (`skewer/src/geometry/boundbox.h`).

### The Slab Method
Skewer checks the overlap of three 1D intervals (the "slabs" between the box's parallel faces).

- **Performance Optimization**: The `Ray` struct precomputes the `inv_direction` ($1.0 / dir$) and `dir_is_neg` signs. This transforms expensive floating-point divisions into fast multiplications in the traversal loop.
- **Robustness**: The implementation handles infinite rays and grazing hits by checking the `t_min` and `t_max` overlap with epsilon padding where necessary.
