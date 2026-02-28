#ifndef SKWR_CORE_SAMPLING_SURFACE_INTERACTION_H_
#define SKWR_CORE_SAMPLING_SURFACE_INTERACTION_H_

#include <cstdint>

#include "core/math/vec3.h"
#include "core/ray.h"

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

    uint16_t exterior_medium;
    uint16_t interior_medium;
    uint16_t priority;

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

#endif  // SKWR_CORE_SAMPLING_SURFACE_INTERACTION_H_
