#include "accelerators/tlas.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/math/transform.h"
#include "geometry/boundbox.h"
#include "geometry/intersect_triangle.h"

namespace skwr {

namespace {

// Converts local-space intersection data back to world space using the same TRS that transformed
// the ray into local space, keeping normals and tangents consistent with the world-space geometry.
static void TransformHitToWorld(const TRS& world_from_local, const Ray& world_ray,
                                SurfaceInteraction* si) {
    si->point = TRSApplyPoint(world_from_local, si->point);
    si->n_geom = TRSApplyNormal(world_from_local, si->n_geom);
    si->n_shading = TRSApplyNormal(world_from_local, si->n_shading);
    si->dpdu = TRSApplyVector(world_from_local, si->dpdu);
    si->dpdv = TRSApplyVector(world_from_local, si->dpdv);
    si->wo = -world_ray.direction();
}

}  // namespace

static constexpr int kSAHBins = 16;
static constexpr float kCostTraverse = 1.0f;
static constexpr float kCostIntersect = 4.0f;

// Build reorders instances for cache-friendly traversal (same assumption as BLAS triangle reorder).
// The caller must keep instances in sync with the TLAS used for intersection.
void TLAS::Build(std::vector<Instance>& instances) {
    if (instances.empty()) {
        nodes_.clear();
        return;
    }

    nodes_.clear();
    nodes_.reserve(instances.size() * 2);

    std::vector<BVHPrimitiveInfo> primitive_info(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        primitive_info[i].original_index = (uint32_t)i;
        // world_bounds is motion-expanded over the shutter interval (computed during scene build).
        primitive_info[i].bounds = instances[i].world_bounds;
        primitive_info[i].bounds.PadToMinimums();
        primitive_info[i].centroid = instances[i].world_bounds.Centroid();
    }

    BVHNode& root = nodes_.emplace_back();
    root.left_first = 0;
    root.tri_count = (uint32_t)instances.size();

    Subdivide(0, 0, (uint32_t)instances.size(), primitive_info, instances);

    // Instances are reordered to match BVH traversal order (same assumption as BLAS triangle
    // reorder).
    std::vector<Instance> ordered;
    ordered.reserve(instances.size());
    for (const auto& info : primitive_info) {
        ordered.push_back(std::move(instances[info.original_index]));
    }
    instances = std::move(ordered);
}

// SAH binning uses instance centroids in world space; degenerate or poorly fitting bounds may
// produce unbalanced splits.
void TLAS::Subdivide(uint32_t node_idx, uint32_t first_inst, uint32_t inst_count,
                     std::vector<BVHPrimitiveInfo>& primitive_info,
                     std::vector<Instance>& instances) {
    BVHNode& node = nodes_[node_idx];

    node.bounds = BoundBox();
    for (uint32_t i = 0; i < inst_count; ++i) {
        node.bounds.Expand(primitive_info[first_inst + i].bounds);
    }

    if (inst_count == 1) {
        node.left_first = first_inst;
        node.tri_count = inst_count;
        return;
    }

    struct Bin {
        BoundBox bounds;
        int count = 0;
    };

    const float leaf_cost = (float)inst_count * kCostIntersect;
    const float parent_area = node.bounds.HalfArea();

    float best_cost = std::numeric_limits<float>::max();
    int best_axis = -1;
    float best_split = 0.0f;

    for (int axis = 0; axis < 3; ++axis) {
        float c_min = std::numeric_limits<float>::max();
        float c_max = std::numeric_limits<float>::lowest();
        for (uint32_t i = 0; i < inst_count; ++i) {
            float c = primitive_info[first_inst + i].centroid[axis];
            c_min = std::fmin(c_min, c);
            c_max = std::fmax(c_max, c);
        }
        if (c_min == c_max) continue;

        Bin bins[kSAHBins] = {};
        const float inv_range = kSAHBins / (c_max - c_min);
        for (uint32_t i = 0; i < inst_count; ++i) {
            const BVHPrimitiveInfo& info = primitive_info[first_inst + i];
            int b = (int)((info.centroid[axis] - c_min) * inv_range);
            if (b >= kSAHBins) b = kSAHBins - 1;
            bins[b].count++;
            bins[b].bounds.Expand(info.bounds);
        }

        BoundBox left_box[kSAHBins - 1];
        int left_cnt[kSAHBins - 1];
        {
            BoundBox cur;
            int cnt = 0;
            for (int k = 0; k < kSAHBins - 1; ++k) {
                cur.Expand(bins[k].bounds);
                cnt += bins[k].count;
                left_box[k] = cur;
                left_cnt[k] = cnt;
            }
        }

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

    if (best_axis == -1 || best_cost >= leaf_cost) {
        node.left_first = first_inst;
        node.tri_count = inst_count;
        return;
    }

    auto start_itr = primitive_info.begin() + first_inst;
    auto end_itr = start_itr + inst_count;
    auto mid_itr = std::partition(start_itr, end_itr, [&](const BVHPrimitiveInfo& info) {
        return info.centroid[best_axis] < best_split;
    });

    uint32_t left_count = (uint32_t)std::distance(start_itr, mid_itr);
    if (left_count == 0 || left_count == inst_count) {
        node.left_first = first_inst;
        node.tri_count = inst_count;
        return;
    }

    uint32_t left_child_idx = (uint32_t)nodes_.size();
    nodes_.emplace_back();
    nodes_.emplace_back();

    nodes_[node_idx].left_first = left_child_idx;
    nodes_[node_idx].tri_count = 0;

    Subdivide(left_child_idx, first_inst, left_count, primitive_info, instances);
    Subdivide(left_child_idx + 1, first_inst + left_count, inst_count - left_count, primitive_info,
              instances);
}

// Ray time drives animation sampling; instances and blases must correspond to TLAS build order.
bool TLAS::Intersect(const Ray& ray, float t_min, float t_max, SurfaceInteraction* si,
                     const std::vector<BLAS>& blases,
                     const std::vector<Instance>& instances) const {
    if (IsEmpty()) return false;

    bool hit_anything = false;
    float closest_t = t_max;

    const Vec3& inv_dir = ray.inv_direction();
    const int dir_is_neg[3] = {inv_dir.x() < 0, inv_dir.y() < 0, inv_dir.z() < 0};

    int nodes_to_visit[64];
    int to_visit_offset = 0;
    nodes_to_visit[0] = 0;

    while (to_visit_offset >= 0) {
        int current_node_idx = nodes_to_visit[to_visit_offset--];
        const BVHNode& node = nodes_[current_node_idx];

        if (node.bounds.Intersect(ray, t_min, closest_t)) {
            if (node.tri_count > 0) {
                for (uint32_t i = 0; i < node.tri_count; ++i) {
                    const Instance& inst = instances[node.left_first + i];
                    // Evaluate instance transform at ray shutter time; static instances use
                    // pre-baked transform to avoid redundant chain evaluation.
                    TRS world_from_local =
                        inst.is_static ? inst.static_world_from_local
                                       : EvaluateTransformChain(inst.transform_chain, ray.time());
                    Ray local_ray(TRSInverseApplyPoint(world_from_local, ray.origin()),
                                  TRSInverseApplyVector(world_from_local, ray.direction()),
                                  ray.time());
                    const BLAS& blas = blases[inst.blas_id];
                    if (blas.bvh.IsEmpty()) continue;

                    uint32_t tri_idx = 0;
                    if (blas.bvh.Intersect(local_ray, t_min, closest_t, si, blas.triangles,
                                           &tri_idx)) {
                        hit_anything = true;
                        closest_t = si->t;
                        TransformHitToWorld(world_from_local, ray, si);
                        // tri_light_indices are in BLAS triangle order (post-BVH reorder).
                        if (tri_idx < inst.tri_light_indices.size()) {
                            si->light_index = inst.tri_light_indices[tri_idx];
                        } else {
                            si->light_index = -1;
                        }
                    }
                }
            } else {
                int axis = node.bounds.LongestAxis();
                if (dir_is_neg[axis]) {
                    nodes_to_visit[++to_visit_offset] = node.left_first;
                    nodes_to_visit[++to_visit_offset] = node.left_first + 1;
                } else {
                    nodes_to_visit[++to_visit_offset] = node.left_first + 1;
                    nodes_to_visit[++to_visit_offset] = node.left_first;
                }
            }
        }
    }
    return hit_anything;
}

}  // namespace skwr
