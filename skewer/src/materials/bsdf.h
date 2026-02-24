#ifndef SKWR_MATERIALS_BSDF_H_
#define SKWR_MATERIALS_BSDF_H_

#include "core/rng.h"
#include "core/spectrum.h"
#include "core/vec3.h"
#include "materials/material.h"
#include "scene/surface_interaction.h"

namespace skwr {

/**
 * Evaluation
 * Returns the BSDF value: f(wo, wi) = Albedo / Pi (reflectance)
 * wo = out vector to camera, wi = in vector to Light/Next bounce
 */
Spectrum EvalBSDF(const Material& mat, const Vec3& wo, const Vec3& wi, const Vec3& n,
                  const SampledWavelengths& wl);

/**
 * PROBABILITY DENSITY (PDF)
 * Returns the probability of sampling direction 'wi'
 */
float PdfBSDF(const Material& mat, const Vec3& wo, const Vec3& wi, const Vec3 n);

inline float Reflectance(float cosine, float refraction_ratio) {
    // Use Schlick's approximation for reflectance.
    auto r0 = (1 - refraction_ratio) / (1 + refraction_ratio);
    r0 = r0 * r0;
    return r0 + (1 - r0) * std::pow((1 - cosine), 5);
}

bool SampleLambertian(const Material& mat, const SurfaceInteraction& si, RNG& rng,
                      const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f);

bool SampleMetal(const Material& mat, const SurfaceInteraction& si, RNG& rng,
                 const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f);

bool SampleDielectric(const Material& mat, const SurfaceInteraction& si, RNG& rng,
                      const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f);

/**
 * This function takes the Incoming Ray and returns two things:
 * - Attenuation: How much light was absorbed (the color).
 * - Scattered Ray: The new direction the photon travels.
 * Dispatches to correct material type sampling function
 */
bool SampleBSDF(const Material& mat, const Ray& r_in, const SurfaceInteraction& si, RNG& rng,
                const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f);

}  // namespace skwr

#endif  // SKWR_MATERIALS_BSDF_H_
