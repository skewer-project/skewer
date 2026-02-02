#include "core/constants.h"
#include "geometry/intersect_sphere.h"
#include "scene/scene.h"

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

    // 2. Check Triangles (Linear Scan - Placeholder for now)
    // for (const auto& tri : triangles_) {
    //    if (IntersectTriangle(r, tri, meshes_, closest_t, si)) { ... }
    // }

    return hit_anything;
}

}  // namespace skwr
