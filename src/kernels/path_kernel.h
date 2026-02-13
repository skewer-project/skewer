#ifndef SKWR_KERNELS_PATH_KERNEL_H_
#define SKWR_KERNELS_PATH_KERNEL_H_

#include <algorithm>

#include "core/constants.h"
#include "core/ray.h"
#include "core/rng.h"
#include "core/spectrum.h"
#include "core/vec3.h"
#include "integrators/path_sample.h"
#include "materials/bsdf.h"
#include "materials/material.h"
#include "scene/scene.h"
#include "scene/surface_interaction.h"
#include "session/render_options.h"

namespace skwr {

inline void AddSegment(PathSample& sample, const float& t_min, const float& t_max,
                       const Spectrum& L, const float& alpha) {
    sample.segments.push_back({t_min, t_max, L, alpha});
}

/**
 * In a recursive renderer (RTIOW), light is calculated as:
 *          Color = DirectLight + Albedo × RecursiveCall()
 * In our iterative renderer, we keep a running variable called Throughput (β or beta).
 * It represents "what fraction of light from the next bounce will actually make it back to the
 * camera?" AKA the fraction of light that survives up to this point (from the camera)
 * Start: β = 1.0 (White)
 * Bounce 1 (Red Wall): β = 1.0 × 0.5(Red) = 0.5
 * Bounce 2 (Grey Floor): β = 0.5 × 0.5(Grey) = 0.25
 * Hit Light (Intensity 10): FinalColor += β × 10 = 2.5
 */
inline PathSample Li(const Ray& ray, const Scene& scene, RNG& rng, const IntegratorConfig& config) {
    PathSample result;
    Spectrum L(0.0f);     // Accumulated Radiance (color)
    Spectrum beta(1.0f);  // Throughput (attenuation)
    Ray r = ray;
    bool specular_bounce = true;
    float t_prev = 0.0f;  // where last segment ended

    // "Bounce" loop - calculates Li: how much Radiance (L) is incoming (i)
    // by multiplying the total light by the amount lost at the end
    for (int depth = 0; depth < config.max_depth; ++depth) {
        SurfaceInteraction si;
        if (!scene.Intersect(r, kShadowEpsilon, kInfinity, &si)) {
            // Environment Segment
            Spectrum env_L = beta * Spectrum(0.0f);
            AddSegment(result, t_prev, kInfinity, env_L, 0.0f);
            L += env_L;
            break;
        }

        // Empty Space Segment (Volume/Air)
        // If we had volumetrics, we would ray-march here and accumulate L/Alpha.
        AddSegment(result, t_prev, si.t, Spectrum(0.0f), 0.0f);

        const Material& mat = scene.GetMaterial(si.material_id);

        // Calculate surface opacity (alpha for this segment)
        Spectrum opacity = mat.opacity;
        float alpha = mat.IsTransparent() ? opacity.Average() : 1.0f;

        /* Emission check for if we hit a light */
        if (mat.IsEmissive()) {
            if (specular_bounce) {
                AddSegment(result, si.t, si.t + kShadowEpsilon, mat.emission, alpha);
                L += beta * mat.emission;
            }
        }

        /* Handle transparency - straight-through transmission */
        if (mat.IsTransparent()) {
            // For non-refractive transparent surfaces (like foliage, smoke, etc.)
            // This is separate from Dielectric refraction

            Spectrum transmittance = Spectrum(1.0f) - opacity;

            if (transmittance.MaxComponent() > 0.0f) {
                // Continue ray through surface for the transmitted portion
                // This requires spawning a transmission ray
                // For now, we'll handle this in the BSDF sampling below
            }
        }

        /* Next Event Estimation */
        if (mat.type != MaterialType::Metal && mat.type != MaterialType::Dielectric &&
            !scene.Lights().empty()) {
            int light_index = int(rng.UniformFloat() * scene.Lights().size());
            const AreaLight& light = scene.Lights()[light_index];
            LightSample ls = Sample_Light(scene, light, rng);

            // Shadow Ray setup
            Vec3 to_light = ls.p - si.point;
            Float dist_sq = to_light.LengthSquared();
            Float dist = std::sqrt(dist_sq);
            Vec3 wi_light = to_light / dist;

            Ray shadow_ray(si.point + (wi_light * kShadowEpsilon), wi_light);
            SurfaceInteraction shadow_si;  // dummy
            if (!scene.Intersect(shadow_ray, 0.f, dist - kShadowEpsilon, &shadow_si)) {
                Float cos_light = std::fmax(0.0f, Dot(-wi_light, ls.n));
                // Area PDF -> Solid Angle PDF: PDF_w = PDF_a * dist^2 / cos_light
                if (cos_light > 0) {
                    Float light_pdf_w = ls.pdf * dist_sq / cos_light;

                    // BSDF Evaluation
                    Float cos_surf = std::fmax(0.0f, Dot(wi_light, si.n_geom));
                    Spectrum f_val = Eval_BSDF(mat, si.wo, wi_light, si.n_geom);

                    // Accumulate
                    // Weight = 1.0 / (N_lights * PDF_w)
                    // L += beta * f * Le * cos_surf * Weight
                    Float selection_prob = 1.0f / scene.Lights().size();
                    Spectrum direct_L =
                        beta * f_val * ls.emission * cos_surf / (light_pdf_w * selection_prob);
                    direct_L *= opacity;

                    AddSegment(result, si.t, si.t + kShadowEpsilon, direct_L, alpha);
                    L += direct_L;
                }
            }
        }

        /* Indirect bounce case */
        Vec3 wi;
        Float pdf;
        Spectrum f;

        /* BSDF check */
        if (Sample_BSDF(mat, r, si, rng, wi, pdf, f)) {
            if (pdf > 0) {
                Float cos_theta = std::abs(Dot(wi, si.n_geom));
                Spectrum weight = f * cos_theta / pdf;  // Universal pdf func now

                // Modulate throughput by opacity for non-specular bounces
                // For Dielectrics/Metals, opacity is typically 1.0
                // For transparent diffuse, we need to account for absorption
                if (mat.type == MaterialType::Lambertian) {
                    weight *= opacity;  // Absorb based on opacity
                }

                beta *= weight;
                r = Ray(si.point + (wi * kShadowEpsilon), wi);

                // If this bounce was sharp (Metal/Glass), next hit counts as specular
                specular_bounce =
                    (mat.type == MaterialType::Metal || mat.type == MaterialType::Dielectric);
            }
        } else {
            // Absorbed (black body)
            AddSegment(result, si.t, si.t + kShadowEpsilon, Spectrum(0.0f), alpha);
            break;
        }

        t_prev = si.t + kShadowEpsilon;

        // Russian Roulette method to kill weak rays early
        // is an optimization cause weak rays = weak influence on final
        if (depth > 3) {
            Float p = std::max(beta.r(), std::max(beta.g(), beta.b()));
            if (rng.UniformFloat() > p) break;
            beta = beta * (1.0f / p);
        }
    }

    result.L = L;
    return result;
}

}  // namespace skwr

#endif  // SKWR_KERNELS_PATH_KERNEL_H_
