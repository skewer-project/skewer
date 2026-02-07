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

// Returns a random direction in the Local Frame (Z is up)
// The probability of picking a direction is proportional to Cosine(theta)
inline Vec3 RandomCosineDirection(RNG& rng) {
    Float r1 = rng.UniformFloat();
    Float r2 = rng.UniformFloat();

    // Standard mapping from unit square to hemisphere
    Float phi = 2.0f * kPi * r1;

    Float x = std::cos(phi) * std::sqrt(r1);  // Sqrt corrects the density
    Float y = std::sin(phi) * std::sqrt(r1);
    Float z = std::sqrt(1.0f - r1);  // This ensures z^2 + r^2 = 1

    return Vec3(x, y, z);
}

// Fully deterministic per-pixel RNG, thread-order independent
inline RNG MakeDeterministicPixelRNG(uint32_t x, uint32_t y, int width, uint32_t sample_index) {
    // Get linear pixel ID
    uint64_t pixel_id = (uint64_t)y * width + x;

    // Mix pixel ID to generate a unique stream (sequence)
    // Use a simple hash / integer mixing function to avoid correlation
    uint64_t seq = pixel_id * 0x9E3779B97F4A7C15ULL;  // golden ratio hash

    // Sample index as the RNG offset
    uint64_t seed = sample_index;

    return RNG(seq, seed);
}

// Power Heuristic for MIS (beta = 2 is standard)
// Calculates the weight for technique 'f' given the probability of 'f' and 'g'
inline Float PowerHeuristic(Float pdf_f, Float pdf_g) {
    Float f2 = pdf_f * pdf_f;
    Float g2 = pdf_g * pdf_g;
    return f2 / (f2 + g2);
}

}  // namespace skwr

#endif  // SKWR_CORE_SAMPLER_H_
