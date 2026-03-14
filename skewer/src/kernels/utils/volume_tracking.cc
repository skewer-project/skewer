#include "kernels/utils/volume_tracking.h"

#include "core/math/constants.h"
#include "core/math/onb.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectrum.h"
#include "media/mediums.h"
#include "scene/scene.h"

namespace skwr {

/**
 * Ratio tracking for shadow rays instead of delta tracking
 * Ratio Tracking treats the volume as partially transparent at every step. It steps forward using
 * the majorant and continuously multiplies the transmittance T_r by the probability of a
 * null collision: T_r ⋅(1 − σ_t(x)/σˉ_t)
 */
Spectrum CalculateGridTransmittance(const GridMedium& medium, const Ray& shadow_ray, float dist,
                                    RNG& rng) {
    float t_min_box = 0.0f;
    float t_max_box = kInfinity;
    if (!medium.bbox.IntersectP(shadow_ray, t_min_box, t_max_box)) return Spectrum(1.0f);

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

Spectrum CalculateTransmittance(const Scene& scene, RNG& rng, const Ray& shadow_ray, float dist) {
    uint16_t active_id = shadow_ray.vol_stack().GetActiveMedium();
    if (active_id == 0 || active_id == kVacuumMediumId) return Spectrum(1.0f);  // Vacuum

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

float EvalHenyeyGreenstein(float g, const Vec3& wo, const Vec3& wi) {
    g = std::clamp(g, -kOneMinusEpsilon, kOneMinusEpsilon);
    float cos_theta = Dot(wo, wi);
    float denom = 1.0f + g * g + 2.0f * g * cos_theta;
    return (1.0f - g * g) / (4.0f * kPi * denom * std::sqrt(denom));
}

void SampleHenyeyGreenstein(float g, const Vec3& wo, float u1, float u2, Vec3& wi) {
    g = std::clamp(g, -kOneMinusEpsilon, kOneMinusEpsilon);
    float cos_theta;
    if (std::abs(g) < kIsotropicPhaseEpsilon) {
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
