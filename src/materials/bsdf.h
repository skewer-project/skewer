#ifndef SKWR_MATERIALS_BSDF_H_
#define SKWR_MATERIALS_BSDF_H_

#include "core/constants.h"
#include "core/onb.h"
#include "core/rng.h"
#include "core/sampling.h"
#include "core/spectrum.h"
#include "materials/material.h"
#include "scene/surface_interaction.h"

namespace skwr {

inline Float Reflectance(Float cosine, Float refraction_ratio) {
    // Use Schlick's approximation for reflectance.
    auto r0 = (1 - refraction_ratio) / (1 + refraction_ratio);
    r0 = r0 * r0;
    return r0 + (1 - r0) * std::pow((1 - cosine), 5);
}

inline bool SampleLambertian(const Material& mat, const SurfaceInteraction& si, RNG& rng,
                             Ray& r_out, Spectrum& attenuation) {
    ONB uvw;
    uvw.BuildFromW(si.n);

    Vec3 local_dir = RandomCosineDirection(rng);
    Vec3 scatter_dir = uvw.Local(local_dir);

    r_out = Ray(si.p + (scatter_dir * kShadowEpsilon), Normalize(scatter_dir));
    attenuation = mat.albedo;
    return true;
}

inline bool SampleMetal(const Material& mat, const SurfaceInteraction& si, RNG& rng, Ray& r_out,
                        Spectrum& attenuation) {
    Vec3 reflected = Reflect(-si.wo, si.n);  // We reflect "incoming view" = -wo

    if (mat.roughness > 0) {
        reflected = Normalize(reflected + (mat.roughness * RandomInUnitSphere(rng)));
    }

    r_out = Ray(si.p + (reflected * kShadowEpsilon), reflected);
    attenuation = mat.albedo;
    return (Dot(r_out.direction(), si.n) > 0);
}

inline bool SampleDielectric(const Material& mat, const SurfaceInteraction& si, RNG& rng,
                             Ray& r_out, Spectrum& attenuation) {
    attenuation = Spectrum(1.0f);

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

    // Push along NEW direction vector
    r_out = Ray(si.p + (direction * kShadowEpsilon), direction);

    return true;
}

/**
 * This function takes the Incoming Ray and returns two things:
 * - Attenuation: How much light was absorbed (the color).
 * - Scattered Ray: The new direction the photon travels.
 * Dispatches to correct material type sampling function
 */
inline bool Sample_BSDF(const Material& mat, const Ray& r_in, const SurfaceInteraction& si,
                        RNG& rng, Ray& r_out, Spectrum& attenuation) {
    switch (mat.type) {
        case MaterialType::Lambertian:
            return SampleLambertian(mat, si, rng, r_out, attenuation);

        case MaterialType::Metal:
            return SampleMetal(mat, si, rng, r_out, attenuation);

        case MaterialType::Dielectric:
            return SampleDielectric(mat, si, rng, r_out, attenuation);  // Implement similarly
    }
    return false;
}

}  // namespace skwr

#endif
