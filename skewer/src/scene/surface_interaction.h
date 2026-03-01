#ifndef SKWR_SCENE_SURFACE_INTERACTION_H_
#define SKWR_SCENE_SURFACE_INTERACTION_H_

#include <cstdint>

#include "core/ray.h"
#include "core/vec3.h"

/* TODO: Implement Deferred Differential Geometry */

namespace skwr {

// "Surface Interaction" is basically a beefed up HitRecord
// It's a "fat" data struct that's calculated immediately
struct SurfaceInteraction {
    Point3 point;     // Exact point of intersection
    Vec3 n_geom;      // Surface normal (geometric)
    Vec3 wo;          // Outgoing direction (points to Camera/viewer)
    float t;          // Distance along ray
    bool front_face;  // Is normal pointing at ray? (Is it the outside face?)
    uint32_t material_id;

    // UV and tangent frame
    Vec3 uv;          // Surface UV (z unused)
    Vec3 dpdu, dpdv;  // Surface tangents (for normal mapping/anisotropy)

    // Shading data
    Vec3 n_shading;  // smooth normal (interpolated)

    // Helper to align normal against the incoming ray
    inline void SetFaceNormal(const Ray& r, const Vec3& outward_normal) {
        wo = -Normalize(r.direction());
        front_face = Dot(r.direction(), outward_normal) < 0;
        n_geom = front_face ? outward_normal : -outward_normal;
    }
};

}  // namespace skwr

#endif  // SKWR_SCENE_SURFACE_INTERACTION_H_
