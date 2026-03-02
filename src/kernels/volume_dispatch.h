#ifndef SKWR_KERNELS_VOLUME_DISPATCH_H_
#define SKWR_KERNELS_VOLUME_DISPATCH_H_

#include <cstdint>

#include "core/math/onb.h"
#include "core/ray.h"
#include "core/sampling/medium_interaction.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectrum.h"
#include "kernels/sample_media.h"
#include "media/mediums.h"
#include "scene/scene.h"

namespace skwr {

/* Volume Dispatcher - returns true if scattering event occurs, false if hit surface */
inline bool SampleMedium(const Ray& ray, const Scene& scene, float t_max, RNG& rng, Spectrum& beta,
                         MediumInteraction& mi, const SampledWavelengths& wl) {
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
            return SampleGrid(scene.grid_media()[index], ray, t_max, rng, beta, mi, wl);
            return false;

        default:
            return false;  // Fallback
    }
}

/**
 * Ratio tracking for shadow rays instead of delta tracking
 * Ratio Tracking treats the volume as partially transparent at every step. It steps forward using
 * the majorant and continuously multiplies the transmittance T_r by the probability of a
 * null collision: T_r ⋅(1 − σ_t(x)/σˉ_t)
 */
inline Spectrum CalculateGridTransmittance(const GridMedium& medium, const Ray& shadow_ray,
                                           float dist, RNG& rng) {
    float t_min_box = 0.0f;
    float t_max_box = 0.0f;
    if (!medium.bbox.Intersect(shadow_ray, t_min_box, t_max_box)) return Spectrum(1.0f);

    float t_min = std::max(0.0f, t_min_box);
    float t_max = std::min(dist, t_max_box);
    if (t_min >= t_max) return Spectrum(1.0f);

    float t = t_min;
    Spectrum Tr(1.0f);

    float majorant =
        medium.max_density * (medium.sigma_a_base + medium.sigma_s_base).MaxComponentValue();
    if (majorant <= 0.0f) return Tr;

    while (true) {
        // Step forward using the majorant
        t += -std::log(std::max(1.0f - rng.UniformFloat(), kEpsilon)) / majorant;
        if (t >= t_max) break;

        // Evaluate actual density at this point
        float density = medium.GetDensity(shadow_ray.at(t));
        Spectrum sigma_t = density * (medium.sigma_a_base + medium.sigma_s_base);

        // Attenuate transmittance by the probability of NOT hitting a particle
        Spectrum null_prob = Spectrum(1.0f) - (sigma_t / majorant);

        for (int i = 0; i < kNSamples; ++i) {
            Tr[i] *= std::max(0.0f, null_prob[i]);
        }

        // Russian Roulette (If transmittance is basically 0 kill early to save expensive
        // VDB/density lookups)
        float max_tr = Tr.MaxComponentValue();
        if (max_tr < 0.05f) {
            float q = std::max(0.05f, 1.0f - max_tr);
            if (rng.UniformFloat() < q) return Spectrum(0.0f);
            Tr = Tr / (1.0f - q);
        }
    }

    return Tr;
}

inline Spectrum CalculateTransmittance(const Scene& scene, RNG& rng, const Ray& shadow_ray,
                                       float dist) {
    uint16_t active_id = shadow_ray.vol_stack().GetActiveMedium();
    if (active_id == 0) return Spectrum(1.0f);  // Vacuum

    MediumType type = static_cast<MediumType>(active_id >> kMediumTypeShift);
    uint16_t index = active_id & kMediumIndexMask;

    switch (type) {
        case MediumType::Homogeneous: {
            const HomogeneousMedium& med = scene.homogeneous_media()[index];
            Spectrum sigma_t = med.Extinction();
            Spectrum tr;
            for (int i = 0; i < kNSamples; ++i) {
                tr[i] = std::exp(-sigma_t[i] * dist);
            }
            return tr;
        }
        case MediumType::Grid: {
            return CalculateGridTransmittance(scene.grid_media()[index], shadow_ray, dist, rng);
        }
        default:
            return Spectrum(1.0f);
    }
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
