#ifndef SKWR_KERNELS_PATH_KERNEL_H_
#define SKWR_KERNELS_PATH_KERNEL_H_

#include <cstdlib>

#include "core/color.h"
#include "core/constants.h"
#include "core/ray.h"
#include "core/rng.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectrum.h"
#include "core/vec3.h"
#include "integrators/path_sample.h"
#include "materials/bsdf.h"
#include "materials/material.h"
#include "materials/texture_lookup.h"
#include "scene/scene.h"
#include "scene/surface_interaction.h"
#include "session/render_options.h"

namespace skwr {

inline void AddSegment(PathSample& sample, const float& t_min, const float& t_max, const RGB& L,
                       const float& alpha) {
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
inline PathSample Li(const Ray& ray, const Scene& scene, RNG& rng, const IntegratorConfig& config,
                     const SampledWavelengths& wl) {
    PathSample result;
    Spectrum L(0.0f);     // Accumulated Radiance (color)
    Spectrum beta(1.0f);  // Throughput (attenuation)
    Ray r = ray;
    bool specular_bounce = true;

    // Deep Info
    bool valid_deep_hit = false;
    Point3 deep_hit_point = r.at(kFarClip);
    Vec3 deep_origin = r.origin();
    float deep_hit_alpha = 1.0f;  // default solid

    // "Bounce" loop - calculates Li: how much Radiance (L) is incoming (i)
    // by multiplying the total light by the amount lost at the end
    for (int depth = 0; depth < config.max_depth; ++depth) {
        SurfaceInteraction si;
        if (!scene.Intersect(r, kShadowEpsilon, kInfinity, &si)) {
            // Primary-ray miss: no geometry hit.
            // In transparent-background mode the environment contributes nothing
            // (alpha stays 0 so compositors see through to layers behind).
            // In opaque mode, treat it as a black environment (alpha = 1).
            if (!config.transparent_background) {
                Spectrum env_L = beta * Spectrum(0.0f);
                L += env_L;
            }
            break;
        }

        // Empty Space Segment (Volume/Air)
        // If we had volumetrics, we would ray-march here and accumulate L/Alpha.
        // AddSegment(result, t_prev, si.t, Spectrum(0.0f), 0.0f);

        const Material& mat = scene.GetMaterial(si.material_id);
        ShadingData sd = ResolveShadingData(mat, si, scene);
        // Lazy Evaluation
        Spectrum opacity(1.0f);
        float alpha = 1.0f;
        if (mat.IsTransparent()) {
            opacity = CurveToSpectrum(mat.opacity, wl);
            alpha = opacity.Average();
        }
        Spectrum emission(0.0f);
        if (mat.IsEmissive()) {
            emission = CurveToSpectrum(mat.emission, wl);
            if (specular_bounce) {
                L += beta * emission;
                deep_hit_point = si.point;  // Record actual emissive surface depth
                valid_deep_hit = true;
            }
        }

        // Record if it's the first deep hit
        if (!valid_deep_hit) {
            // For simplicity, just have all hits update the depth
            // and we rely on the loop finishing to define the color.
            deep_hit_point = si.point;
            // For volumetrics, we RAY MARCH here from r.origin to si.point
            // and AddSegment() continuously.
        }

        // /* Handle transparency - straight-through transmission */
        // if (mat.IsTransparent()) {
        //     // For non-refractive transparent surfaces (like foliage, smoke, etc.)
        //     // This is separate from Dielectric refraction

        //     Spectrum transmittance = Spectrum(1.0f) - opacity;

        //     if (transmittance.MaxComponent() > 0.0f) {
        //         // Continue ray through surface for the transmitted portion
        //         // This requires spawning a transmission ray
        //         // For now, we'll handle this in the BSDF sampling below
        //     }
        // }

        /* Next Event Estimation */
        if (mat.type != MaterialType::Metal && mat.type != MaterialType::Dielectric &&
            !scene.Lights().empty()) {
            int light_index = int(rng.UniformFloat() * scene.Lights().size());
            const AreaLight& light = scene.Lights()[light_index];
            LightSample ls = SampleLight(scene, light, rng);

            // Shadow Ray setup
            Vec3 to_light = ls.p - si.point;
            float dist_sq = to_light.LengthSquared();
            float dist = std::sqrt(dist_sq);
            Vec3 wi_light = to_light / dist;

            Ray shadow_ray(si.point + (wi_light * kShadowEpsilon), wi_light);
            SurfaceInteraction shadow_si;  // dummy
            if (!scene.Intersect(shadow_ray, 0.f, dist - 2.0f * kShadowEpsilon, &shadow_si)) {
                float cos_light = std::fmax(0.0f, Dot(-wi_light, ls.n));
                // Area PDF -> Solid Angle PDF: PDF_w = PDF_a * dist^2 / cos_light
                if (cos_light > 0) {
                    float light_pdf_w = ls.pdf * dist_sq / cos_light;

                    // BSDF Evaluation
                    float cos_surf = std::fmax(0.0f, Dot(wi_light, sd.n_shading));
                    Spectrum f_val = EvalBSDF(mat, sd, si.wo, wi_light, wl);

                    // Accumulate
                    // Weight = 1.0 / (N_lights * PDF_w)
                    // L += beta * f * Le * cos_surf * Weight
                    Spectrum light_spec = CurveToSpectrum(ls.emission, wl);
                    Spectrum direct_L = beta * f_val * light_spec * cos_surf /
                                        (light_pdf_w * scene.InvLightCount());
                    direct_L *= opacity;
                    L += direct_L;
                }
            }
        }

        /* Indirect bounce case */
        Vec3 wi;
        float pdf;
        Spectrum f;

        /* BSDF check */
        if (SampleBSDF(mat, sd, r, si, rng, wl, wi, pdf, f)) {
            if (pdf > 0) {
                float refract = Dot(wi, si.n_geom);

                if (!valid_deep_hit) {
                    if (!(refract < 0.0f)) {
                        valid_deep_hit = true;  // it's a reflection
                    }
                }

                float cos_theta = std::abs(refract);
                Spectrum weight = f * cos_theta / pdf;  // Universal pdf func now

                // Modulate throughput by opacity for non-specular bounces
                // For Dielectrics/Metals, opacity is typically 1.0
                // For transparent diffuse, we need to account for absorption
                if (mat.type == MaterialType::Lambertian) {
                    weight *= alpha;  // Absorb based on opacity
                }

                beta *= weight;
                r = Ray(si.point + (wi * kShadowEpsilon), wi);

                // If this bounce was sharp (Metal/Glass), next hit counts as specular
                specular_bounce =
                    (mat.type == MaterialType::Metal || mat.type == MaterialType::Dielectric);
            }
        } else {
            // Absorbed (black body)
            break;
        }

        // Russian Roulette
        if (depth > 3) {
            float max_beta = beta.MaxComponentValue();
            if (max_beta < 0.001f) break;
            float p = std::min(0.95f, max_beta);
            if (rng.UniformFloat() > p) break;
            beta = beta * (1.0f / p);
        }
    }

    RGB final_rgb = SpectrumToRGB(L, wl);

    // Flat-pass alpha: 1 if any foreground geometry was hit, 0 on a pure miss.
    // With transparent_background=false a miss is treated as an opaque background
    // so alpha stays 1 (backward-compatible behaviour).
    result.alpha = valid_deep_hit ? 1.0f : (config.transparent_background ? 0.0f : 1.0f);

    if (valid_deep_hit) {
        Vec3 to_hit = deep_hit_point - deep_origin;
        float z_depth = Dot(to_hit, config.cam_w);
        // Ensure we don't get negative depth behind camera
        if (z_depth < 0.0f) z_depth = 0.0f;
        AddSegment(result, z_depth, z_depth + kShadowEpsilon, final_rgb, deep_hit_alpha);
    } else if (!config.transparent_background) {
        // Opaque mode: record the miss as a solid far-clip sample so the deep
        // pixel still represents a fully-covered (black) background.
        AddSegment(result, kFarClip, kFarClip + 1000.0f, final_rgb, deep_hit_alpha);
    }
    // transparent_background + no hit → empty segments → alpha=0 in deep output.

    result.L = L;
    return result;
}

}  // namespace skwr

#endif  // SKWR_KERNELS_PATH_KERNEL_H_
