#ifndef SKWR_MATERIALS_BSDF_H_
#define SKWR_MATERIALS_BSDF_H_

#include "core/constants.h"
#include "core/onb.h"
#include "core/rng.h"
#include "core/sampling.h"
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
inline Spectrum Eval_BSDF(const Material& mat, const Vec3& wo, const Vec3& wi, const Vec3& n) {
    // Specular materials (Metal, Glass) are Dirac Deltas (infinity) at the right angle, 0 otherwise
    // so just return Black here because Sample() should handle them
    if (mat.type != MaterialType::Lambertian) return Spectrum(0.f);

    Float cosine = Dot(wi, n);
    if (cosine <= 0) return Spectrum(0.f);  // if light coming from below surface, block it
    return mat.albedo * (1.0f / kPi);       // Lambertian is constant
}

/**
 * PROBABILITY DENSITY (PDF)
 * Returns the probability of sampling direction 'wi'
 */
inline Float Pdf_BSDF(const Material& mat, const Vec3& wo, const Vec3& wi, const Vec3 n) {
    if (mat.type != MaterialType::Lambertian) return 0.f;

    Float cosine = Dot(wi, n);
    if (cosine <= 0) return 0.f;
    return cosine * (1.0f / kPi);  // Cos-weighted sampling
}

inline Float Reflectance(Float cosine, Float refraction_ratio) {
    // Use Schlick's approximation for reflectance.
    auto r0 = (1 - refraction_ratio) / (1 + refraction_ratio);
    r0 = r0 * r0;
    return r0 + (1 - r0) * std::pow((1 - cosine), 5);
}

inline bool SampleLambertian(const Material& mat, const SurfaceInteraction& si, RNG& rng, Vec3& wi,
                             Float& pdf, Spectrum& f) {
    ONB uvw;
    uvw.BuildFromW(si.n);

    Vec3 local_dir = RandomCosineDirection(rng);
    wi = uvw.Local(local_dir);

    // Explicit PDF and Eval
    Float cosine = std::fmax(0.0f, Dot(wi, si.n));
    pdf = cosine / kPi;
    f = mat.albedo * (1.0f / kPi);
    return true;
}

inline bool SampleMetal(const Material& mat, const SurfaceInteraction& si, RNG& rng, Vec3& wi,
                        Float& pdf, Spectrum& f) {
    wi = Reflect(-si.wo, si.n);  // We reflect "incoming view" = -wo
    if (mat.roughness > 0) {
        wi = Normalize(wi + (mat.roughness * RandomInUnitSphere(rng)));
    }

    // Check if valid (above surface)
    Float cosine = Dot(wi, si.n);
    if (cosine <= 0) return false;

    // Delta Distribution Logic
    pdf = 1.0f;
    f = mat.albedo / cosine;  // Cancels the cosine in the rendering equation
    return true;
}

inline bool SampleDielectric(const Material& mat, const SurfaceInteraction& si, RNG& rng, Vec3& wi,
                             Float& pdf, Spectrum& f) {
    // if front_face, we are entering the glass. If false, trying to leave
    Float refraction_ratio = si.front_face ? 1.0f / mat.ior : mat.ior;
    Vec3 unit_direction = -si.wo;  // wo points out to camera. -wo is the IN direction

    Float cos_theta = std::fmin(Dot(si.wo, si.n), 1.0f);
    Float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

    // Total Internal Reflection
    bool cannot_refract = refraction_ratio * sin_theta > 1.0f;

    // Fresnel + scatter
    Vec3 direction;
    if (cannot_refract || Reflectance(cos_theta, refraction_ratio) > rng.UniformFloat()) {
        direction = Reflect(unit_direction, si.n);
    } else {
        direction = Refract(unit_direction, si.n, refraction_ratio);
    }

    // Delta distr logic
    pdf = 1.0f;
    f = Spectrum(1.0f) / std::abs(Dot(wi, si.n));  // Cancels cosine
    return true;
}

/**
 * This function takes the Incoming Ray and returns two things:
 * - Attenuation: How much light was absorbed (the color).
 * - Scattered Ray: The new direction the photon travels.
 * Dispatches to correct material type sampling function
 */
inline bool Sample_BSDF(const Material& mat, const Ray& r_in, const SurfaceInteraction& si,
                        RNG& rng, Vec3& wi, Float& pdf, Spectrum& f) {
    switch (mat.type) {
        case MaterialType::Lambertian:
            return SampleLambertian(mat, si, rng, wi, pdf, f);

        case MaterialType::Metal:
            return SampleMetal(mat, si, rng, wi, pdf, f);

        case MaterialType::Dielectric:
            return SampleDielectric(mat, si, rng, wi, pdf, f);  // Implement similarly
    }
    return false;
}

}  // namespace skwr

#endif
