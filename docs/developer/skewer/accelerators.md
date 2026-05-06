# Accelerators (BVH & TLAS)

Spatial acceleration is the most computationally expensive part of the rendering pipeline. In ray tracing, cache misses during traversal is often the real bottleneck — not arithmetic calculations. Skewer's acceleration system is designed for **maximum traversal performance** in an offline rendering context, prioritizing cache efficiency over build speed.

## Linear BVH Design Decisions

Skewer implements a **Sibling-Level Depth-First Linear BVH** (`skewer/src/accelerators/bvh.cc`). Unlike real-time engines that might prioritize fast refitting (for dynamic geometry), Skewer's design is heavily influenced by high-end offline renderers like PBRT and Arnold.

### 1. Implicit Sibling Layout
Each internal `BVHNode` stores a single `left_first` index. 

- The **Left Child** is located at `nodes[left_first]`.
- The **Right Child** is implicitly at `nodes[left_first + 1]`.

By ensuring siblings are always adjacent in memory, we optimize for cache locality. When visiting a parent node, you almost always test both children. Since they live at `left_first` and `left_first + 1`, they’re likely in the same cache line or at worst in the next sequential line.

Furthermore:

- We avoid storing an extra pointer or index because the right child is implicit (+1)
- That leads to smaller node size → more nodes per cache line
- Fewer memory reads during traversal
- Simpler code path (no need to fetch two indices)
- Branch prediction benefits because traversal becomes more regular
- SIMD-friendly traversal (testing two child AABBs together)
- Potential future GPU layouts (even if Skewer is CPU-focused)

### 2. SAH with Global Binning
To construct the tree, we use the **Surface Area Heuristic (SAH)**. 

- **Binning**: Instead of sorting all primitives at every split (which is $O(N \log^2 N)$), we use **16 bins** along each axis. This reduces construction complexity to $O(N)$ per level.
- **Cost Balancing**: We use a traversal cost ($C_t = 1.0$) and an intersection cost ($C_i = 4.0$). This 1:4 ratio was determined through empirical testing; it prevents the BVH from becoming too deep (wasting time in nodes) or too shallow (wasting time in triangles).

### 3. Primitive Reordering
During the `Build()` phase, Skewer **physically reorders** the triangles in the scene's memory to match the leaf-node order.

Standard BVHs point to indices. Skewer points to the actual triangle array. By reordering the array to match the traversal order, we eliminate "pointer hopping" and maximize data cache locality during the hot intersection loop.

## TLAS & BLAS (Two-Tier Hierarchy)

To support massive scenes without exploding memory usage, Skewer uses a two-tier hierarchy.

### Bottom-Level (BLAS)
- Built over raw `Triangle` geometry.
- Centered in **Local Space**.
- Shared across multiple instances (e.g., a single "Tree" BLAS can be used for 10,000 instances).

### Top-Level (TLAS)
- Built over `Instance` objects in **World Space**.
- **Transformation Handling**: During traversal, if the TLAS hits an instance, the ray is transformed into the BLAS local space. 
- **Design Decision**: Skewer calculates the TLAS world-bounds by motion-expanding the BLAS bounds over the entire shutter interval (`shutter_open` to `shutter_close`). This ensures that even for fast-moving objects, the acceleration structure remains valid for the entire frame without needing to be rebuilt during the path trace.

## Comparative Considerations

During development, we also considered **KD-Trees** and **Octrees**:

- **KD-Trees**: Yield slightly tighter splits but suffer from massive memory overhead due to primitive duplication (a triangle can live in multiple leaves).
- **Linear BVH**: Chosen because it provides a predictable memory footprint and is significantly more robust for the non-uniform triangle distributions common in complex OBJ meshes.

Linear BVH provided the most benefit for simplest implementation in the early stages of development.
