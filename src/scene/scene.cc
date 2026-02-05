#include "scene/scene.h"

#include "core/constants.h"
#include "geometry/intersect_sphere.h"
#include "geometry/intersect_triangle.h"
#include "geometry/mesh.h"

namespace skwr {

bool Scene::Intersect(const Ray &r, Float t_min, Float t_max, SurfaceInteraction *si) const {
    bool hit_anything = false;
    Float closest_t = t_max;
    // 1. Check Spheres (Linear Scan)
    for (const auto &sphere : spheres_) {
        // We pass 'closest_t' as the new max distance to prune objects behind the hit
        if (IntersectSphere(r, sphere, t_min, closest_t, si)) {
            hit_anything = true;
            closest_t = si->t;  // Update closest hit distance
        }
    }

    // Check Triangles (Linear Scan - Placeholder for now)
    for (const auto &tri : triangles_) {
        const Mesh &mesh = meshes_[tri.mesh_id];
        if (IntersectTriangle(r, tri, mesh, t_min, closest_t, si)) {
            hit_anything = true;
            closest_t = si->t;
        }
    }

    return hit_anything;
}

}  // namespace skwr
