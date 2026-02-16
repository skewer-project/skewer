#include "scene/scene.h"

#include <cstdint>

#include "accelerators/bvh.h"
#include "core/constants.h"
#include "core/vec3.h"
#include "geometry/intersect_triangle.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "materials/material.h"
#include "scene/light.h"
#include "scene/surface_interaction.h"

namespace skwr {

void Scene::Build() {
    // only if we have triangles
    if (!triangles_.empty()) {
        std::cout << "Building BVH for " << triangles_.size() << " triangles...\n";
        bvh_.Build(triangles_, meshes_);
    }
}

auto Scene::Intersect(const Ray& r, Float t_min, Float t_max, SurfaceInteraction* si) -> bool {
    bool hit_anything = false;
    Float const closest_t = t_max;
    // 1. Check Spheres (Linear Scan)
    for (const auto& sphere : spheres_) {
        // We pass 'closest_t' as the new max distance to prune objects behind the hit
        if (IntersectSphere(r, sphere, t_min, closest_t, si)) {
            hit_anything = true;
            closest_t = si->t;  // Update closest hit distance
        }
    }

    // BVH
    if (IntersectBVH(r, t_min, closest_t, si)) {
        hit_anything = true;
    }

    return hit_anything;
}

auto Scene::IntersectBVH(const Ray& r, Float t_min, Float t_max, SurfaceInteraction* si) -> bool {
    if (bvh_.IsEmpty()) { return false;
}

    bool hit_anything = false;
    Float closest_t = t_max;

    // using precomputed inverse for aabb check
    const Vec3& inv_dir = r.inv_direction();
    const int dir_is_neg[3] = {static_cast<const int>(inv_dir.x() < 0), static_cast<const int>(inv_dir.y() < 0), static_cast<const int>(inv_dir.z() < 0)};

    // stack of 64 is standard 2^64 triangles = a lot of tris
    int nodes_to_visit[64];
    int to_visit_offset = 0;

    nodes_to_visit[0] = 0;
    while (to_visit_offset >= 0) {
        // pop from stack
        int const current_node_idx = nodes_to_visit[to_visit_offset--];
        const BVHNode& node = bvh_.GetNodes()[current_node_idx];

        // Calculate bbox intersection
        if (node.bounds.Intersect(r, t_min, closest_t)) {
            // if leaf, intersect the triangles
            if (node.tri_count > 0) {
                for (uint32_t i = 0; i < node.tri_count; ++i) {
                    const Triangle& tri = triangles_[node.left_first + i];
                    const Mesh& mesh = meshes_[tri.mesh_id];

                    if (IntersectTriangle(r, tri, mesh, t_min, closest_t, si)) {
                        hit_anything = true;
                        closest_t = si->t;
                    }
                }
            }
            // else it's internal node -> push children onto stack
            else {
                // Check closest child first to find hit faster. If found, closest_t shrinks and
                // might be able to skip far child entirely
                int const axis = node.bounds.LongestAxis();

                // if ray dir is negative along the split axis, right child is closer
                if (dir_is_neg[axis] != 0) {
                    nodes_to_visit[++to_visit_offset] = node.left_first;      // Far (push first)
                    nodes_to_visit[++to_visit_offset] = node.left_first + 1;  // Near (pop next)
                } else {
                    nodes_to_visit[++to_visit_offset] = node.left_first + 1;  // Far
                    nodes_to_visit[++to_visit_offset] = node.left_first;      // Near
                }
            }
        }
    }
    return hit_anything;
}

static uint32_t Scene::AddSphere(const Sphere& s) {
    spheres_.push_back(s);
    uint32_t sphere_index = (uint32_t)spheres_.size() - 1;
    const Material& mat = materials_[s.material_id];
    if (mat.IsEmissive()) {
        // Create the Light Wrapper
        AreaLight light;
        light.type = AreaLight::Sphere;
        light.primitive_index = sphere_index;
        light.emission = mat.emission;
        lights_.push_back(light);
    }
    return sphere_index;
}

uint32_t Scene::AddMaterial(const Material& m) {
    materials_.push_back(m);
    return static_cast<uint32_t>(materials_.size() - 1);
}

static uint32_t Scene::AddMesh(Mesh&& m) {
    meshes_.push_back(std::move(m));
    uint32_t mesh_id = (uint32_t)meshes_.size() - 1;
    const Material& mat = materials_[m.material_id];

    // AUTO-GENERATE TRIANGLES
    // When we add a mesh, we immediately break it into Triangle primitives
    // so the renderer can see them.
    const Mesh& mesh_ref = meshes_.back();
    for (size_t i = 0; i < mesh_ref.indices.size(); i += 3) {
        Triangle const t;
        t.mesh_id = mesh_id;
        t.v_idx = (uint32_t)i;  // Points to the first index of the triplet
        triangles_.push_back(t);

        if (mat.IsEmissive()) {
            AreaLight light;
            light.type = AreaLight::Triangle;
            light.primitive_index = (uint32_t)triangles_.size() - 1;
            light.emission = mat.emission;
            lights_.push_back(light);
        }
    }

    return mesh_id;
}

}  // namespace skwr
