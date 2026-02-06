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
    Point3 p;         // Exact point of intersection
    Vec3 n;           // Surface normal (geometric)
    Vec3 wo;          // Outgoing direction (points to Camera/viewer)
    Float t;          // Distance along the ray
    bool front_face;  // Is normal pointing at ray? (Is it the outside face?)
    uint32_t material_id;

    // Future: UVs, dpdu, dpdv, Material*

    // Helper to align normal against the incoming ray
    inline void SetFaceNormal(const Ray& r, const Vec3& outward_normal) {
        wo = -Normalize(r.direction());
        front_face = Dot(r.direction(), outward_normal) < 0;
        n = front_face ? outward_normal : -outward_normal;
    }
};

}  // namespace skwr

#endif  // SKWR_SCENE_SURFACE_INTERACTION_H_
