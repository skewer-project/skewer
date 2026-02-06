#include "accelerators/bvh.h"

#include <algorithm>  // for std::partition
#include <cstdint>

#include "geometry/boundbox.h"

namespace skwr {

// TODO: Optimze by precomputing centroid/bounds later...
// Helper to get centroid of a triangle
Vec3 GetCentroid(const Triangle& t, const std::vector<Mesh>& meshes) {
    const Mesh& m = meshes[t.mesh_id];
    Vec3 p0 = m.p[m.indices[t.v_idx]];
    Vec3 p1 = m.p[m.indices[t.v_idx + 1]];
    Vec3 p2 = m.p[m.indices[t.v_idx + 2]];
    return (p0 + p1 + p2) * (1.0f / 3.0f);
}

// Helper to get bounds of a triangle
BoundBox GetBounds(const Triangle& t, const std::vector<Mesh>& meshes) {
    const Mesh& m = meshes[t.mesh_id];
    BoundBox bbox(m.p[m.indices[t.v_idx]]);
    bbox.Expand(m.p[m.indices[t.v_idx + 1]]);
    bbox.Expand(m.p[m.indices[t.v_idx + 2]]);
    return bbox;
}

void BVH::Build(std::vector<Triangle>& triangles, const std::vector<Mesh>& meshes) {
    if (triangles.empty()) return;

    // Reset
    nodes_.clear();
    nodes_.reserve(triangles.size() * 2);  // mem alloc

    // Create Root Node
    BVHNode& root = nodes_.emplace_back();  // we not using pushback for efficiency
    root.left_first = 0;
    root.tri_count = (uint32_t)triangles.size();

    // Start recursion
    Subdivide(0, 0, (uint32_t)triangles.size(), triangles, meshes);
}

void BVH::Subdivide(uint32_t node_idx, uint32_t first_tri, uint32_t tri_count,
                    std::vector<Triangle>& triangles, const std::vector<Mesh>& meshes) {
    BVHNode& node = nodes_[node_idx];

    // Calculate Bounds for this node
    // We loop through the range of triangles assigned and grow bounding volume
    node.bounds = BoundBox();  // Reset to invalid

    for (uint32_t i = 0; i < tri_count; ++i) {
        BoundBox tri_box = GetBounds(triangles[first_tri + i], meshes);
        node.bounds.Expand(tri_box);
    }

    node.bounds.PadToMinimums();

    // Leaf Stop condition
    // Stop if we have few triangles, to avoid overhead of deep trees
    if (tri_count <= 2) {
        node.left_first = first_tri;  // Point to start of triangle index
        node.tri_count = tri_count;   // Mark as leaf (count > 0)
        return;
    }

    // TODO: SAH with bin/bucketing (16)
    // Split at longest box axis
    int axis = node.bounds.LongestAxis();
    Float split_pos = node.bounds.Centroid()[axis];

    // alternative: split_pos = (node.bounds.min()[axis] + node.bounds.max()[axis]) * 0.5f;

    // Partition the triangle array by moving triangles in the vector
    // Iterators point to the range [first_tri, first_tri + tri_count)
    auto start_itr = triangles.begin() + first_tri;
    auto end_itr = start_itr + tri_count;

    auto mid_itr = std::partition(start_itr, end_itr, [&](const Triangle& t) {
        return GetCentroid(t, meshes)[axis] < split_pos;
    });

    // Calculate how many ended up on the left
    uint32_t left_count = (uint32_t)std::distance(start_itr, mid_itr);

    // CHECK: Did partition fail? (All tris on one side?)
    // This happens if all centroids are identical (overlapping geometry)
    if (left_count == 0 || left_count == tri_count) {
        node.left_first = first_tri;
        node.tri_count = tri_count;  // Make it a leaf
        return;
    }

    // Create Children
    // We allocate 2 nodes contiguously.
    // Depth-first but sibling-contiguous basically
    uint32_t left_child_idx = (uint32_t)nodes_.size();
    nodes_.emplace_back();
    nodes_.emplace_back();

    // Re-fetch node (emplace_back invalidates references)
    nodes_[node_idx].left_first = left_child_idx;
    nodes_[node_idx].tri_count = 0;  // Mark as INTERNAL (count = 0)

    // Recurse
    Subdivide(left_child_idx, first_tri, left_count, triangles, meshes);
    Subdivide(left_child_idx + 1, first_tri + left_count, tri_count - left_count, triangles,
              meshes);
}

}  // namespace skwr
