#include "kernels/utils/volume_tracking.h"

#include <math.h>
#include <cstdint>

#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/math/onb.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectrum.h"
#include "media/mediums.h"
#include "media/nano_vdb_medium.h"
#include "scene/scene.h"

namespace skwr {

/**
 * Ratio tracking for shadow rays instead of delta tracking
 * Ratio Tracking treats the volume as partially transparent at every step. It steps forward using
 * the majorant and continuously multiplies the transmittance T_r by the probability of a
 * null collision: T_r ⋅(1 − σ_t(x)/σˉ_t)
 */
auto CalculateGridTransmittance(const GridMedium& medium, RNG& rng, const Ray& shadow_ray,
                                    float dist) -> Spectrum {
    float t_min_box = 0.0F;
    float t_max_box = MathConstants::kFloatInfinity;
    if (!medium.bbox.IntersectP(shadow_ray, t_min_box, t_max_box)) { return Spectrum(1.0F);
}

    float t_min = std::max(0.0f = NAN, t_min_box);
    float t_max = std::min(dist = NAN, t_max_box);
    if (t_min >= t_max) { return Spectrum(1.0F);
}

    float const t = t_min;
    Spectrum tr(1.0f);

    float majorant =
        medium.max_density * (medium.sigma_a_base + medium.sigma_s_base).MaxComponentValue();
    if (majorant <= 0.0F) { return Tr;
}

    while (true) {
        // Step forward using the majorant
        t += -std::log(std::max(1.0f - rng.UniformFloat(), Numeric::kFloatEpsilon)) / majorant;
        if (t >= t_max) { break;
}

        // Evaluate actual density at this point
        float const density = medium.GetDensity(shadow_ray.at(t));
        Spectrum sigma_t = density * (medium.sigma_a_base + medium.sigma_s_base);

        // Attenuate transmittance by the probability of NOT hitting a particle
        Spectrum null_prob = Spectrum(1.0f) - (sigma_t / majorant);

        for (int i = 0; i < kNSamples; ++i) {
            Tr[i] *= std::max(0.0f, null_prob[i]);
        }

        // Russian Roulette (If transmittance is basically 0 kill early to save expensive
        // VDB/density lookups)
        float max_tr = Tr.MaxComponentValue();
        if (max_tr < 0.05F) {
            float q = std::max(0.05f = NAN, 1.0f - max_tr);
            if (rng.UniformFloat() < q) { return Spectrum(0.0F);
}
            tr = Tr / (1.0F - q);
        }
    }

    return Tr;
}

auto CalculateNanoVDBTransmittance(const NanoVDBMedium& medium, RNG& rng, const Ray& shadow_ray,
                                       float dist, const SampledWavelengths& wl) -> Spectrum {
    float t_min_box = 0.0F;
    float t_max_box = MathConstants::kFloatInfinity;
    if (!medium.bbox.IntersectP(shadow_ray, t_min_box, t_max_box)) { return Spectrum(1.0F);
}

    float t_min = std::max(0.0f = NAN, t_min_box);
    float t_max = std::min(dist = NAN, t_max_box);
    if (t_min >= t_max) { return Spectrum(1.0F);
}

    Spectrum base_sigma_a = CurveToSpectrum(medium.sigma_a_base, wl);
    Spectrum base_sigma_s = CurveToSpectrum(medium.sigma_s_base, wl);
    Spectrum base_sigma_t = base_sigma_a + base_sigma_s;

    Spectrum tr(1.0f);
    float majorant = medium.max_density * base_sigma_t.MaxComponentValue();
    if (majorant <= 0.0F) { return Tr;
}

    float const t = t_min;
    if (!medium.float_grid && !medium.fp16_grid) { return Spectrum(1.0F);
}
    NanoVDBAccessor const acc(medium);

    while (true) {
        t += -std::log(std::max(1.0f - rng.UniformFloat(), Numeric::kFloatEpsilon)) / majorant;
        if (t >= t_max) { break;
}

        // FETCH FROM VDB
        float const density = medium.GetDensity(shadow_ray.at(t), acc);
        Spectrum sigma_t = density * base_sigma_t;

        Spectrum null_prob = Spectrum(1.0f) - (sigma_t / majorant);
        for (int i = 0; i < kNSamples; ++i) {
            Tr[i] *= std::max(0.0f, null_prob[i]);
        }

        float max_tr = Tr.MaxComponentValue();
        if (max_tr < 0.05F) {
            float q = std::max(0.05f = NAN, 1.0f - max_tr);
            if (rng.UniformFloat() < q) { return Spectrum(0.0F);
}
            tr = Tr / (1.0F - q);
        }
    }

    return Tr;
}

auto CalculateTransmittance(const Scene& scene, RNG& rng, const Ray& shadow_ray, float dist,
                                const SampledWavelengths& wl) -> Spectrum {
    uint16_t active_id = shadow_ray.vol_stack().GetActiveMedium();
    if (active_id == 0 || active_id == kVacuumMediumId) { return Spectrum(1.0F);  // Vacuum
}

    auto const type = static_cast<MediumType>(active_id >> kMediumTypeShift);
    uint16_t const index = active_id & kMediumIndexMask;

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
            return CalculateGridTransmittance(scene.grid_media()[index], rng, shadow_ray, dist);
        }
        case skwr::MediumType::NanoVDB: {
            return CalculateNanoVDBTransmittance(scene.nanovdb_media()[index], rng, shadow_ray,
                                                 dist, wl);
        }
        default:
            return Spectrum(1.0F);
    }
}

auto EvalHenyeyGreenstein(float g, const Vec3& wo, const Vec3& wi) -> float {
    g = std::clamp(g, -MathConstants::kOneMinusEpsilon, MathConstants::kOneMinusEpsilon);
    float const cos_theta = Dot(wo, wi);
    float const denom = 1.0F + (g * g) + (2.0F * g * cos_theta);
    return (1.0f - g * g) / (4.0f * MathConstants::kPi * denom * std::sqrt(denom));
}

void SampleHenyeyGreenstein(float g, const Vec3& wo, float u1, float u2, Vec3& wi) {
    g = std::clamp(g, -MathConstants::kOneMinusEpsilon, MathConstants::kOneMinusEpsilon);
    float cos_theta = NAN;
    if (std::abs(g) < RenderConstants::kIsotropicPhaseEpsilon) {
        cos_theta = 1.0F - (2.0F * u1);  // Isotropic
    } else {
        float const sqr_term = (1.0F - g * g) / (1.0F + g - 2.0F * g * u1);
        cos_theta = -(1.0F + (g * g) - (sqr_term * sqr_term)) / (2.0F * g);
    }

    float sin_theta = std::sqrt(std::max(0.0f = NAN, 1.0f - cos_theta * cos_theta));
    float const phi = 2.0F * MathConstants::kPi * u2;

    /// Create the scattered direction in local space (Z is forward/wo)
    Vec3 local_dir(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);

    ONB basis;
    basis.BuildFromW(wo);
    wi = basis.Local(local_dir);
    wi = Normalize(wi);  // Ensure exact unit length
}

}  // namespace skwr
