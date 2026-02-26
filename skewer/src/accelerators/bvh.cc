#include "accelerators/bvh.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "geometry/boundbox.h"

namespace skwr {

// ---------------------------------------------------------------------------
// SAH constants
// ---------------------------------------------------------------------------
static constexpr int kSAHBins = 16;
static constexpr float kCostTraverse = 1.0f;   // relative cost of an AABB test
static constexpr float kCostIntersect = 4.0f;  // relative cost of a triangle test

// ---------------------------------------------------------------------------
// Helpers using pre-baked Triangle data (no mesh indirection)
// ---------------------------------------------------------------------------

static Vec3 GetCentroid(const Triangle& t) {
    // centroid = (p0 + p1 + p2) / 3  =  p0 + (e1 + e2) / 3
    return t.p0 + (t.e1 + t.e2) * (1.0f / 3.0f);
}

static BoundBox GetBounds(const Triangle& t) {
    BoundBox bbox(t.p0);
    bbox.Expand(t.p0 + t.e1);
    bbox.Expand(t.p0 + t.e2);
    return bbox;
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

void BVH::Build(std::vector<Triangle>& triangles) {
    if (triangles.empty()) return;

    nodes_.clear();
    nodes_.reserve(triangles.size() * 2);

    std::vector<BVHPrimitiveInfo> primitive_info(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        primitive_info[i].original_index = (uint32_t)i;
        primitive_info[i].bounds = GetBounds(triangles[i]);
        primitive_info[i].bounds.PadToMinimums();
        primitive_info[i].centroid = GetCentroid(triangles[i]);
    }

    BVHNode& root = nodes_.emplace_back();
    root.left_first = 0;
    root.tri_count = (uint32_t)triangles.size();

    Subdivide(0, 0, (uint32_t)triangles.size(), primitive_info);

    // Reorder triangles to match the BVH-ordered primitive_info
    std::vector<Triangle> ordered;
    ordered.reserve(triangles.size());
    for (const auto& info : primitive_info) {
        ordered.push_back(triangles[info.original_index]);
    }
    triangles = std::move(ordered);
}

// ---------------------------------------------------------------------------
// Subdivide — SAH binning over all 3 axes
// ---------------------------------------------------------------------------

void BVH::Subdivide(uint32_t node_idx, uint32_t first_tri, uint32_t tri_count,
                    std::vector<BVHPrimitiveInfo>& primitive_info) {
    BVHNode& node = nodes_[node_idx];

    // Compute tight bounds for this node
    node.bounds = BoundBox();
    for (uint32_t i = 0; i < tri_count; ++i) {
        node.bounds.Expand(primitive_info[first_tri + i].bounds);
    }

    // A single triangle cannot be split further
    if (tri_count == 1) {
        node.left_first = first_tri;
        node.tri_count = tri_count;
        return;
    }

    // -----------------------------------------------------------------------
    // SAH binning: evaluate kSAHBins-1 candidate splits on each of 3 axes.
    // Cost model:  C = C_traverse + (SA_L/SA_parent)*N_L*C_isect
    //                             + (SA_R/SA_parent)*N_R*C_isect
    // We minimise N_L*SA_L + N_R*SA_R (denominator is constant per node).
    // -----------------------------------------------------------------------
    struct Bin {
        BoundBox bounds;  // default-constructed = invalid (+Inf/-Inf)
        int count = 0;
    };

    const float leaf_cost = (float)tri_count * kCostIntersect;
    const float parent_area = node.bounds.HalfArea();

    float best_cost = std::numeric_limits<float>::max();
    int best_axis = -1;
    float best_split = 0.0f;

    for (int axis = 0; axis < 3; ++axis) {
        // Find centroid range along this axis
        float c_min = std::numeric_limits<float>::max();
        float c_max = std::numeric_limits<float>::lowest();
        for (uint32_t i = 0; i < tri_count; ++i) {
            float c = primitive_info[first_tri + i].centroid[axis];
            c_min = std::fmin(c_min, c);
            c_max = std::fmax(c_max, c);
        }
        if (c_min == c_max) continue;  // all centroids coincide on this axis

        // Assign each triangle to a bin
        Bin bins[kSAHBins] = {};
        const float inv_range = kSAHBins / (c_max - c_min);
        for (uint32_t i = 0; i < tri_count; ++i) {
            const BVHPrimitiveInfo& info = primitive_info[first_tri + i];
            int b = (int)((info.centroid[axis] - c_min) * inv_range);
            if (b >= kSAHBins) b = kSAHBins - 1;
            bins[b].count++;
            bins[b].bounds.Expand(info.bounds);
        }

        // Left prefix: left_box[k] = union(bins[0..k])
        //              left_cnt[k] = count in bins[0..k]
        BoundBox left_box[kSAHBins - 1];
        int left_cnt[kSAHBins - 1];
        {
            BoundBox cur;
            int cnt = 0;
            for (int k = 0; k < kSAHBins - 1; ++k) {
                cur.Expand(bins[k].bounds);  // safe even if bins[k] is empty
                cnt += bins[k].count;
                left_box[k] = cur;
                left_cnt[k] = cnt;
            }
        }

        // Right suffix: right_box[k] = union(bins[k+1..N-1])
        //               right_cnt[k] = count in bins[k+1..N-1]
        BoundBox right_box[kSAHBins - 1];
        int right_cnt[kSAHBins - 1];
        {
            BoundBox cur;
            int cnt = 0;
            for (int k = kSAHBins - 1; k >= 1; --k) {
                cur.Expand(bins[k].bounds);
                cnt += bins[k].count;
                right_box[k - 1] = cur;
                right_cnt[k - 1] = cnt;
            }
        }

        // Evaluate each candidate split boundary
        const float bin_size = (c_max - c_min) / kSAHBins;
        for (int k = 0; k < kSAHBins - 1; ++k) {
            if (left_cnt[k] == 0 || right_cnt[k] == 0) continue;
            float cost = kCostTraverse + kCostIntersect *
                                             (left_cnt[k] * left_box[k].HalfArea() +
                                              right_cnt[k] * right_box[k].HalfArea()) /
                                             parent_area;
            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                best_split = c_min + (k + 1) * bin_size;
            }
        }
    }

    // If no split is cheaper than a leaf, make a leaf
    if (best_axis == -1 || best_cost >= leaf_cost) {
        node.left_first = first_tri;
        node.tri_count = tri_count;
        return;
    }

    // Partition triangles along the best split
    auto start_itr = primitive_info.begin() + first_tri;
    auto end_itr = start_itr + tri_count;
    auto mid_itr = std::partition(start_itr, end_itr, [&](const BVHPrimitiveInfo& info) {
        return info.centroid[best_axis] < best_split;
    });

    uint32_t left_count = (uint32_t)std::distance(start_itr, mid_itr);
    // Safety: degenerate partition shouldn't happen given the SAH cost guard, but check anyway
    if (left_count == 0 || left_count == tri_count) {
        node.left_first = first_tri;
        node.tri_count = tri_count;
        return;
    }

    // Allocate two contiguous child nodes (invalidates node reference — re-fetch via index)
    uint32_t left_child_idx = (uint32_t)nodes_.size();
    nodes_.emplace_back();
    nodes_.emplace_back();

    nodes_[node_idx].left_first = left_child_idx;
    nodes_[node_idx].tri_count = 0;  // mark as internal

    Subdivide(left_child_idx, first_tri, left_count, primitive_info);
    Subdivide(left_child_idx + 1, first_tri + left_count, tri_count - left_count, primitive_info);
}

}  // namespace skwr
