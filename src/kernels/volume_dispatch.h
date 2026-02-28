#ifndef SKWR_KERNELS_VOLUME_DISPATCH_H_
#define SKWR_KERNELS_VOLUME_DISPATCH_H_

#include <cstdint>

#include "core/math/onb.h"
#include "core/ray.h"
#include "core/sampling/medium_interaction.h"
#include "core/sampling/rng.h"
#include "kernels/sample_homogeneous.h"
#include "media/mediums.h"
#include "scene/scene.h"

namespace skwr {

/* Volume Dispatcher - returns true if scattering event occurs, false if hit surface */
inline bool SampleMedium(const Ray& ray, const Scene& scene, float t_max, RNG& rng, Spectrum& beta,
                         MediumInteraction& mi) {
    uint16_t active_id = ray.vol_stack().GetActiveMedium();

    // Decode the Bit-Packed ID
    uint16_t type = active_id >> kMediumTypeShift;
    uint16_t index = active_id & kMediumIndexMask;

    switch (type) {
        case static_cast<int>(MediumType::Vacuum):
            // No attenuation, ray passes straight to the surface.
            return false;

        case static_cast<int>(MediumType::Homogeneous):
            return SampleHomogeneous(scene.homogeneous_media()[index], ray, t_max, rng, beta, mi);

        case static_cast<int>(MediumType::Grid):
            //     return SampleGrid(grid_media[index], ray, t_max, rng, beta, mi);
            return false;

        default:
            return false;  // Fallback
    }
}

inline Spectrum CalculateTransmittance(const Scene& scene, const Ray& shadow_ray, float dist) {
    uint16_t active_id = shadow_ray.vol_stack().GetActiveMedium();
    if (active_id == 0) return Spectrum(1.0f);  // Vacuum

    MediumType type = static_cast<MediumType>(active_id >> 14);
    uint16_t index = active_id & 0x3FFF;

    if (type == MediumType::Homogeneous) {
        const HomogeneousMedium& med = scene.homogeneous_media()[index];
        Spectrum sigma_t = med.Extinction();
        Spectrum tr;
        for (int i = 0; i < kNSamples; ++i) {
            tr[i] = std::exp(-sigma_t[i] * dist);
        }
        return tr;
    }
    return Spectrum(1.0f);  // Fallback
}

inline float EvalHG(float g, const Vec3& wo, const Vec3& wi) {
    float cos_theta = Dot(wo, wi);
    float denom = 1.0f + g * g + 2.0f * g * cos_theta;
    return (1.0f - g * g) / (4.0f * kPi * denom * std::sqrt(denom));
}

inline void SampleHG(float g, const Vec3& wo, float u1, float u2, Vec3& wi) {
    float cos_theta;
    if (std::abs(g) < 1e-3f) {
        cos_theta = 1.0f - 2.0f * u1;  // Isotropic
    } else {
        float sqr_term = (1.0f - g * g) / (1.0f + g - 2.0f * g * u1);
        cos_theta = -(1.0f + g * g - sqr_term * sqr_term) / (2.0f * g);
    }

    float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
    float phi = 2.0f * kPi * u2;

    /// Create the scattered direction in local space (Z is forward/wo)
    Vec3 local_dir(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);

    ONB basis;
    basis.BuildFromW(wo);
    wi = basis.Local(local_dir);
    wi = Normalize(wi);  // Ensure exact unit length
}

}  // namespace skwr

#endif  // SKWR_KERNELS_VOLUME_DISPATCH_H_
