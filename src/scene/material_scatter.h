#ifndef SKWR_SCENE_MATERIAL_SCATTER_H_
#define SKWR_SCENE_MATERIAL_SCATTER_H_

#include "core/constants.h"
#include "core/rng.h"
#include "core/sampling.h"
#include "core/spectrum.h"
#include "core/vec3.h"
#include "materials/material.h"
#include "scene/surface_interaction.h"

namespace skwr {

inline Float Reflectance(Float cosine, Float refraction_ratio) {
    // Use Schlick's approximation for reflectance.
    auto r0 = (1 - refraction_ratio) / (1 + refraction_ratio);
    r0 = r0 * r0;
    return r0 + (1 - r0) * std::pow((1 - cosine), 5);
}

/**
 * This function takes the Incoming Ray and returns two things:
 * - Attenuation: How much light was absorbed (the color).
 * - Scattered Ray: The new direction the photon travels.
 */
// Returns true if the ray scatters, false if it is absorbed (black body)
inline bool Scatter(const Material &mat, const Ray &r_in, const SurfaceInteraction &si, RNG &rng,
                    Spectrum &attenuation, Ray &r_scattered) {
    attenuation = mat.albedo;  // Most materials just tint the light ig
    switch (mat.type) {
        //------------------------------------------------------------------------------
        // Lambertian (diffuse) material
        //------------------------------------------------------------------------------
        case MaterialType::Lambertian: {
            Vec3 scatter_direction = si.n + RandomUnitVector(rng);
            // degenerate scatter direction case (if random is opposite of normal)
            if (scatter_direction.Near_zero()) scatter_direction = si.n;

            r_scattered = Ray(si.p + (si.n * kShadowEpsilon), Normalize(scatter_direction));
            return true;
        }
        //------------------------------------------------------------------------------
        // Metal (reflective) material
        //------------------------------------------------------------------------------
        case MaterialType::Metal: {
            Vec3 reflected = Reflect(Normalize(r_in.direction()), si.n);

            // Fuzziness (roughness)
            // Add a small random sphere to tip of reflection vector
            if (mat.roughness > 0) {
                reflected = reflected + (mat.roughness * RandomInUnitSphere(rng));
            }

            r_scattered = Ray(si.p + (si.n * kShadowEpsilon), reflected);

            return (Dot(r_scattered.direction(), si.n) > 0);
        }
        //------------------------------------------------------------------------------
        // Dielectric (glass/transparent) material
        //------------------------------------------------------------------------------
        case MaterialType::Dielectric: {
            attenuation = Spectrum(1.0f);  // Glass doesnt usually absorb light

            Float refraction_ratio = mat.ior;  // Assume air->glass until ior stack implemented

            // If dot(ray, normal) < 0, we are outside hitting the front.
            // If dot > 0, we are inside trying to get out.
            bool front_face = Dot(r_in.direction(), si.n) < 0;
            Vec3 normal = front_face ? si.n : -si.n;
            if (front_face) refraction_ratio = 1.0 / mat.ior;

            Vec3 unit_direction = Normalize(r_in.direction());

            // Total Internal Reflection check
            Float cos_theta = std::fmin(Dot(-unit_direction, normal), 1.0f);
            Float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

            bool cannot_refract = refraction_ratio * sin_theta > 1.0;

            // Schlick's approx for fresnel
            // Glass refracts more when seen edge-on
            Vec3 direction;
            if (cannot_refract || Reflectance(cos_theta, refraction_ratio) > rng.UniformFloat())
                direction = Reflect(unit_direction, normal);
            else
                direction = Refract(unit_direction, normal, refraction_ratio);

            r_scattered = Ray(si.p + (normal * kShadowEpsilon), direction);
            return true;
        }
    }
    return false;
}

}  // namespace skwr

#endif  // SKWR_SCENE_MATERIAL_SCATTER_H_
