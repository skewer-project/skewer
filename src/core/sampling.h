#ifndef SKWR_CORE_SAMPLER_H_
#define SKWR_CORE_SAMPLER_H_

#include "core/constants.h"
#include "core/rng.h"
#include "core/vec3.h"

namespace skwr {

// Helper func. Generates random float in [min, max) using explicit RNG
inline float RandomFloat(RNG& rng, float min, float max) {
    return min + (max - min) * rng.UniformFloat();
}

// Generating arbitrary random vectors
inline Vec3 RandomVec3(RNG& rng) {
    return Vec3(rng.UniformFloat(), rng.UniformFloat(), rng.UniformFloat());
}

inline Vec3 RandomVec3(RNG& rng, float min, float max) {
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
    float r1 = rng.UniformFloat();
    float r2 = rng.UniformFloat();

    // Standard mapping from unit square to hemisphere
    float phi = 2.0f * kPi * r1;

    float x = std::cos(phi) * std::sqrt(r1);  // Sqrt corrects the density
    float y = std::sin(phi) * std::sqrt(r1);
    float z = std::sqrt(1.0f - r1);  // This ensures z^2 + r^2 = 1

    return Vec3(x, y, z);
}

// Stripped down PCG hash - just something better than using golden ratio
inline uint32_t StrongHash(uint32_t input) {
    uint32_t state = input * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Fully deterministic per-pixel RNG, thread-order independent
inline RNG MakeDeterministicPixelRNG(uint32_t x, uint32_t y, int width, uint32_t sample_index) {
    // Get linear pixel ID
    uint32_t pixel_id = (uint32_t)y * width + x;

    // Scramble the pixel ID to pick a mathematically distinct PCG sequence
    uint64_t seq = StrongHash(pixel_id);

    // Mix the pixel ID and starting sample to create a unique initial state
    uint64_t seed = StrongHash(pixel_id ^ StrongHash(sample_index));

    return RNG(seq, seed);
}

// Power Heuristic for MIS (beta = 2 is standard)
// Calculates the weight for technique 'f' given the probability of 'f' and 'g'
inline float PowerHeuristic(float pdf_f, float pdf_g) {
    float f2 = pdf_f * pdf_f;
    float g2 = pdf_g * pdf_g;
    return f2 / (f2 + g2);
}

}  // namespace skwr

#endif  // SKWR_CORE_SAMPLER_H_
