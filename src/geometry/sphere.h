#ifndef SKWR_GEOMETRY_SPHERE_H_
#define SKWR_GEOMETRY_SPHERE_H_

#include <cstdint>

#include "core/vec3.h"

namespace skwr {

struct Sphere {
    Vec3 center;
    float radius;
    uint32_t material_id;
};

}  // namespace skwr

#endif  // SKWR_GEOMETRY_SPHERE_H_

// Force 16-byte align for easy mapping to GPU float4 in future alignas(16)
// Implicit padding rn but explicit better for GPU later
// like float _pad or something?
