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

    // Deep / alpha tracking
    //
    // camera_visible: a material with visible=true was hit within the first
    //   visibility_depth bounces.  Governs result.alpha and whether a deep
    //   segment is emitted. When transparent_background=false this is unused
    //   (all geometry counts).
    //
    // valid_deep_hit: a surface that should contribute depth info was hit.
    //   In transparent mode this is gated on visibility so invisible geometry
    //   does not lock in a depth that the camera should never see.
    bool camera_visible = false;
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

        // --- Visibility window check ---
        // Update camera_visible if this bounce is within the window AND the
        // surface is marked visible.  This is the only place camera_visible
        // is ever set; everything else just reads it.
        if (!camera_visible && mat.visible && depth < config.visibility_depth) {
            camera_visible = true;
        }

        // A surface "counts for depth" in transparent mode only when it is
        // visible (or we are not in transparent mode at all).  This prevents
        // invisible walls from locking the deep hit-point before the visible
        // sphere is encountered.
        const bool counts_for_depth = !config.transparent_background || mat.visible;

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
                if (counts_for_depth) {
                    deep_hit_point = si.point;
                    valid_deep_hit = true;
                }
            }
        }

        // Roll the deep hit-point forward to the current surface while we
        // have not yet committed.  Only update for surfaces that count for
        // depth (visible ones in transparent mode; any in opaque mode).
        if (!valid_deep_hit && counts_for_depth) {
            deep_hit_point = si.point;
            // For volumetrics, we would RAY MARCH here from r.origin to
            // si.point and AddSegment() continuously.
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

                if (!valid_deep_hit && counts_for_depth) {
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

    // In transparent mode a pixel is "covered" only when a visible object was
    // hit within the visibility_depth window.  In opaque mode every geometry
    // hit counts (backward-compatible).
    const bool covered =
        config.transparent_background ? camera_visible : (valid_deep_hit || camera_visible);
    result.alpha = covered ? 1.0f : (config.transparent_background ? 0.0f : 1.0f);

    // Deep segment: emit when we have a committed depth AND the pixel is covered.
    // In opaque mode the far-clip fallback ensures even pure misses are represented.
    if (valid_deep_hit && covered) {
        Vec3 to_hit = deep_hit_point - deep_origin;
        float z_depth = Dot(to_hit, config.cam_w);
        if (z_depth < 0.0f) z_depth = 0.0f;
        AddSegment(result, z_depth, z_depth + kShadowEpsilon, final_rgb, deep_hit_alpha);
    } else if (!config.transparent_background) {
        // Opaque mode: solid far-clip sample so the deep pixel is always covered.
        AddSegment(result, kFarClip, kFarClip + 1000.0f, final_rgb, deep_hit_alpha);
    }
    // transparent + !covered → no segments → compositor sees fully transparent pixel.

    result.L = L;
    return result;
}

}  // namespace skwr

#endif  // SKWR_KERNELS_PATH_KERNEL_H_
