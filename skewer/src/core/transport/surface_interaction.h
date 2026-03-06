#ifndef SKWR_CORE_TRANSPORT_SURFACE_INTERACTION_H_
#define SKWR_CORE_TRANSPORT_SURFACE_INTERACTION_H_

#include <cstdint>

#include "core/math/vec3.h"

/* TODO: Implement Deferred Differential Geometry */

namespace skwr {

// "Surface Interaction" is basically a beefed up HitRecord
// It's a "fat" data struct that's calculated immediately
struct SurfaceInteraction {
    Point3 point;  // Exact point of intersection
    Vec3 n_geom;   // Surface normal (geometric)
    Vec3 wo;       // Outgoing direction (points to Camera/viewer)
    float t;       // Distance along ray
    uint32_t material_id;

    uint16_t exterior_medium;
    uint16_t interior_medium;
    uint16_t priority;

    // UV and tangent frame
    Vec3 uv;          // Surface UV (z unused)
    Vec3 dpdu, dpdv;  // Surface tangents (for normal mapping/anisotropy)

    // Shading data
    Vec3 n_shading;  // smooth normal (interpolated)
};

}  // namespace skwr

#endif  // SKWR_CORE_TRANSPORT_SURFACE_INTERACTION_H_
