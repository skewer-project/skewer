# Accelerators (BVH & TLAS)

This section covers the spatial acceleration structures used in Skewer to speed up ray-primitive intersection tests.

## BVH (Bounding Volume Hierarchy)

Skewer uses a high-performance, linear BVH to organize scene geometry (`skewer/src/accelerators/bvh.cc`).

### Memory Layout & Cache Locality
The BVH is strictly "linear" and stack-based. Nodes are stored in a contiguous `std::vector<BVHNode>` using a depth-first ordering.

```cpp
struct alignas(32) BVHNode {
    BoundBox bounds; // 24 bytes (min: 12, max: 12)
    uint32_t left_first; // 4 bytes
    uint32_t tri_count; // 4 bytes
};
```
The `alignas(32)` directive ensures each node fits perfectly into half a cache line. 
- **Leaf Nodes** (`tri_count > 0`): `left_first` is the index of the first triangle in the reordered triangle array.
- **Internal Nodes** (`tri_count == 0`): `left_first` is the index of the left child. The right child is always at `left_first + 1`.

### Construction (Surface Area Heuristic)
Skewer builds the BVH using a top-down **SAH** approach. It balances the cost of traversing nodes vs. intersecting triangles.

1. **Binning**: Skewer uses 16 bins (`kSAHBins = 16`) along each axis to evaluate split candidates in $O(N)$ time.
2. **Cost Model**: 
   $Cost = C_{traverse} + C_{intersect} \cdot \frac{\sum (SA_{child} \cdot N_{child})}{SA_{parent}}$
3. **Partitioning**: Triangles are partitioned using `std::partition` along the axis and split point with the minimum cost.
4. **Reordering**: After construction, the global triangle array is reordered to match the depth-first leaf structure, maximizing cache hits during the trace.

## TLAS & BLAS (Instancing)

To support massive scenes with duplicated geometry, Skewer uses a two-tier hierarchy:

1. **BLAS (Bottom-Level Acceleration Structure)**: A BVH built over the local-space triangles of a mesh.
2. **TLAS (Top-Level Acceleration Structure)**: A BVH built over scene `Instance` objects.

### Traversal Logic
When a ray traverses the TLAS and hits an `Instance`:
1. The ray is transformed into **Local Space** using the instance's inverse TRS.
2. The local ray traverses the **BLAS**.
3. Upon a hit, the `SurfaceInteraction` point and normal are transformed back into **World Space** using the instance's forward TRS.
