#include "scene/scene.h"

#include <cstdint>

#include "accelerators/bvh.h"
#include "core/vec3.h"
#include "geometry/intersect_sphere.h"
#include "geometry/intersect_triangle.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "materials/material.h"
#include "scene/surface_interaction.h"

namespace skwr {

void Scene::Build() {
    triangles_.clear();
    lights_.clear();

    // Re-register sphere lights
    for (uint32_t i = 0; i < (uint32_t)spheres_.size(); ++i) {
        const Material& mat = materials_[spheres_[i].material_id];
        if (mat.IsEmissive()) {
            AreaLight light;
            light.type = AreaLight::Sphere;
            light.primitive_index = i;
            light.emission = mat.emission;
            lights_.push_back(light);
        }
    }

    // Bake one Triangle per mesh face, capturing final vertex positions,
    // edges, normals, and material_id from the fully-prepared Mesh objects.
    for (uint32_t mesh_id = 0; mesh_id < (uint32_t)meshes_.size(); ++mesh_id) {
        const Mesh& mesh_ref = meshes_[mesh_id];

        for (size_t i = 0; i < mesh_ref.indices.size(); i += 3) {
            uint32_t i0 = mesh_ref.indices[i];
            uint32_t i1 = mesh_ref.indices[i + 1];
            uint32_t i2 = mesh_ref.indices[i + 2];

            Triangle t;
            t.p0 = mesh_ref.p[i0];
            t.e1 = mesh_ref.p[i1] - t.p0;
            t.e2 = mesh_ref.p[i2] - t.p0;
            t.material_id = mesh_ref.material_id;
            t.needs_tangent_frame = mat.HasNormalMap();

            if (!mesh_ref.n.empty()) {
                t.n0 = mesh_ref.n[i0];
                t.n1 = mesh_ref.n[i1];
                t.n2 = mesh_ref.n[i2];
            } else {
                Vec3 geom_n = Normalize(Cross(t.e1, t.e2));
                t.n0 = t.n1 = t.n2 = geom_n;
            }

            if (!mesh_ref.uv.empty()) {
                t.uv0 = mesh_ref.uv[i0];
                t.uv1 = mesh_ref.uv[i1];
                t.uv2 = mesh_ref.uv[i2];
            } else {
                t.uv0 = t.uv1 = t.uv2 = Vec3(0.0f, 0.0f, 0.0f);
            }

            triangles_.push_back(t);
        }
    }

    if (!triangles_.empty()) {
        std::cout << "Building BVH for " << triangles_.size() << " triangles...\n";
        bvh_.Build(triangles_);
    }

    for (uint32_t i = 0; i < (uint32_t)triangles_.size(); ++i) {
        const Material& mat = materials_[triangles_[i].material_id];
        if (mat.IsEmissive()) {
            AreaLight light;
            light.type = AreaLight::Triangle;
            light.primitive_index = i;
            light.emission = mat.emission;
            lights_.push_back(light);
        }
    }
    inv_light_count_ = 1.0f / lights_.size();
}

bool Scene::Intersect(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const {
    bool hit_anything = false;
    float closest_t = t_max;
    for (const auto& sphere : spheres_) {
        if (IntersectSphere(r, sphere, t_min, closest_t, si)) {
            hit_anything = true;
            closest_t = si->t;
        }
    }

    if (IntersectBVH(r, t_min, closest_t, si)) {
        hit_anything = true;
    }

    return hit_anything;
}

bool Scene::IntersectBVH(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const {
    if (bvh_.IsEmpty()) return false;

    bool hit_anything = false;
    float closest_t = t_max;

    const Vec3& inv_dir = r.inv_direction();
    const int dir_is_neg[3] = {inv_dir.x() < 0, inv_dir.y() < 0, inv_dir.z() < 0};

    int nodes_to_visit[64];
    int to_visit_offset = 0;

    nodes_to_visit[0] = 0;
    while (to_visit_offset >= 0) {
        int current_node_idx = nodes_to_visit[to_visit_offset--];
        const BVHNode& node = bvh_.GetNodes()[current_node_idx];

        if (node.bounds.Intersect(r, t_min, closest_t)) {
            if (node.tri_count > 0) {
                for (uint32_t i = 0; i < node.tri_count; ++i) {
                    const Triangle& tri = triangles_[node.left_first + i];
                    if (IntersectTriangle(r, tri, t_min, closest_t, si)) {
                        hit_anything = true;
                        closest_t = si->t;
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

uint32_t Scene::AddSphere(const Sphere& s) {
    spheres_.push_back(s);
    return (uint32_t)spheres_.size() - 1;
}

uint32_t Scene::AddMaterial(const Material& m) {
    materials_.push_back(m);
    return static_cast<uint32_t>(materials_.size() - 1);
}

uint32_t Scene::AddMesh(Mesh&& m) {
    meshes_.push_back(std::move(m));
    return (uint32_t)meshes_.size() - 1;
}

uint32_t Scene::AddTexture(ImageTexture&& t) {
    textures_.push_back(std::move(t));
    return static_cast<uint32_t>(textures_.size() - 1);
}

}  // namespace skwr
