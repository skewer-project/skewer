#ifndef SKWR_CORE_SAMPLER_H_
#define SKWR_CORE_SAMPLER_H_

#include "core/rng.h"
#include "core/vec3.h"

namespace skwr {

// Helper func. Generates random float in [min, max) using explicit RNG
inline Float RandomFloat(RNG& rng, Float min, Float max) {
    return min + (max - min) * rng.UniformFloat();
}

// Generating arbitrary random vectors
inline Vec3 RandomVec3(RNG& rng) {
    return Vec3(rng.UniformFloat(), rng.UniformFloat(), rng.UniformFloat());
}

inline Vec3 RandomVec3(RNG& rng, Float min, Float max) {
    return Vec3(RandomFloat(rng, min, max), RandomFloat(rng, min, max), RandomFloat(rng, min, max));
}

// Rejection method for generating random vector on surface of a unit sphere
inline Vec3 RandomInUnitSphere(RNG& rng) {
    while (true) {
        // Generate vector between -1 and 1
        auto p = RandomVec3(rng, -1, 1);
        if (p.LengthSquared() < 1) return p;
    }
}
inline Vec3 RandomUnitVector(RNG& rng) { return Normalize(RandomInUnitSphere(rng)); }

// Check if unit vector is on the same hemisphere as normal (want it pointing away from surface)
inline Vec3 RandomOnHemisphere(RNG& rng, const Vec3& normal) {
    Vec3 on_unit_sphere = RandomUnitVector(rng);
    if (Dot(on_unit_sphere, normal) > 0.0)  // aligned with normal
        return on_unit_sphere;
    else
        return -on_unit_sphere;
}

// Defocus disk
inline Vec3 RandomInUnitDisk(RNG& rng) {
    while (true) {
        auto p = Vec3(RandomFloat(rng, -1, 1), RandomFloat(rng, -1, 1), 0);
        if (p.LengthSquared() < 1) return p;
    }
}

}  // namespace skwr

#endif  // SKWR_CORE_SAMPLER_H_
