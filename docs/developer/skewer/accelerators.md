# Accelerators (BVH & TLAS)

Spatial acceleration is the most computationally expensive part of the rendering pipeline. In ray tracing, cache misses during traversal is often the real bottleneck — not arithmetic calculations. Skewer's acceleration system is designed for **maximum traversal performance** in an offline rendering context, prioritizing cache efficiency over build speed.

Spatial acceleration is the most computationally expensive part of the rendering pipeline. In ray tracing, cache misses during traversal is often the real bottleneck — not arithmetic calculations. Skewer's acceleration system is designed for maximum traversal performance in an offline rendering context, prioritizing cache efficiency over build speed.

Unlike games that need to rebuild acceleration structures 60 times a second, Skewer only needs to build the acceleration structures once. Thus, we are willing to spend more time during the `Build()` phase (evaluating SAH bins and reordering memory) if it results in a 5% reduction in the subsequent multi-hour rendering pass.

## Directory Reference

The following sections detail the implementations within the `skewer/src/accelerators/` directory.

### Linear BVH

Skewer implements a **Sibling-Level Depth-First Linear BVH**. Unlike real-time engines that might prioritize fast refitting (for dynamic geometry), Skewer's design is heavily influenced by high-end offline renderers like PBRT and Arnold.

#### Implicit Sibling Layout
Each internal `BVHNode` stores a single `left_first` index. 
- The **Left Child** is located at `nodes[left_first]`.
- The **Right Child** is implicitly at `nodes[left_first + 1]`.

By ensuring siblings are always adjacent in memory, we optimize for cache locality. When visiting a parent node, you almost always test both children. Since they live at `left_first` and `left_first + 1`, they’re likely in the same cache line or at worst in the next sequential line.

Furthermore:
- We avoid storing an extra pointer or index because the right child is implicit (+1).
- That leads to smaller node size → more nodes per cache line.
- Fewer memory reads during traversal.
- SIMD-friendly traversal (testing two child AABBs together).

#### SAH with Global Binning
To construct the tree, we use the **Surface Area Heuristic (SAH)**. 

- **Binning**: Instead of sorting all primitives at every split (which is $O(N \log^2 N)$), we use **16 bins** along each axis. This reduces construction complexity to $O(N)$ per level.
- **Cost Balancing**: We use a traversal cost ($C_t = 1.0$) and an intersection cost ($C_i = 4.0$). This 1:4 ratio prevents the BVH from becoming too deep (wasting time in nodes) or too shallow (wasting time in triangles).

#### Primitive Reordering
During the `Build()` phase, Skewer reorders the triangles in the scene's memory to match the leaf-node order. Standard BVHs point to indices; Skewer points to the actual triangle array. By reordering the array to match the traversal order, we eliminate "pointer hopping" and maximize data cache locality during the hot intersection loop.

### Top-Level Acceleration

The **Top-Level Acceleration Structure (TLAS)** is a BVH built over `Instance` objects in **World Space**.

- **Transformation Handling**: During traversal, if the TLAS hits an instance, the ray is transformed into the BLAS local space using the instance's inverse TRS.
- **Motion Blur Support**: Skewer calculates the TLAS world-bounds by motion-expanding the BLAS bounds over the entire shutter interval (`shutter_open` to `shutter_close`). This ensures that even for fast-moving objects, the acceleration structure remains valid for the entire frame without needing to be rebuilt during the path trace.

### Bottom-Level Acceleration

The **Bottom-Level Acceleration Structure (BLAS)** is a BVH built over raw `Triangle` geometry in **Local Space**.

- **Geometry Sharing**: A single BLAS (e.g., a high-polygon tree) can be referenced by thousands of `Instance` objects, allowing for massive scenes without proportional memory growth.
- **Independence**: Each BLAS is built once and remains static, regardless of how many times it is instanced or where those instances are placed in the world.

### Scene Instances

An `Instance` acts as the bridge between a BLAS and the World. It contains:
- A reference to a `BLAS`.
- A **TRS Transform Chain** (potentially animated).
- **Baked Metadata**: Light indices and world-space bounds.

---

## Comparative Considerations

During development, we considered **KD-Trees** and **Octrees**:
- **KD-Trees**: Yield slightly tighter splits but suffer from massive memory overhead due to primitive duplication (a triangle can live in multiple leaves).
- **Linear BVH**: Chosen because it provides a predictable memory footprint and is significantly more robust for the non-uniform triangle distributions common in complex OBJ meshes.

Linear BVH provided the most benefit for simplest implementation in the early stages of development.
