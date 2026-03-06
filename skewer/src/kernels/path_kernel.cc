#include "kernels/path_kernel.h"

#include <cstdlib>

#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectral/spectrum.h"
#include "core/transport/deep_path_recorder.h"
#include "core/transport/medium_interaction.h"
#include "core/transport/path_sample.h"
#include "core/transport/surface_interaction.h"
#include "kernels/utils/direct_lighting.h"
#include "kernels/utils/visibility.h"
#include "kernels/utils/volume_tracking.h"
#include "kernels/volume_dispatch.h"
#include "materials/bsdf.h"
#include "materials/material.h"
#include "materials/texture_lookup.h"
#include "scene/scene.h"
#include "scene/skybox.h"
#include "session/render_options.h"

namespace skwr {

/**
 * Iterative path tracer:
 * |- For depth:
 *      |- Check closest surface
 *      |- Check volume with surface hit as t_max
 *
 *      |- If volume scatter:
 *          |- NEE
 *          |- Append PathVertex
 *
 *      |- If surface scatter:
 *          |- Check transport/shading for medium boundaries
 *          |- Emissive check
 *          |- NEE
 *          |- BSDF
 *      |- Else: environment sample
 *
 * |- Deferred Deep Output pass
 */
PathSample Li(const Ray& ray, const Scene& scene, RNG& rng, const IntegratorConfig& config,
              const SampledWavelengths& wl) {
    PathSample result;
    Spectrum L(0.0f);     // Accumulated Radiance (color)
    Spectrum beta(1.0f);  // Throughput (attenuation)
    Ray r = ray;
    bool specular_bounce = true;

    float ray_t = 0.0f;  // Running parametric distance

    DeepPathRecorder dpr(config.max_depth);  // Deferred State tracker

    bool is_camera_path = true;

    for (int depth = 0; depth < config.max_depth; ++depth) {  // TODO: switch to while?
        SurfaceInteraction si;
        MediumInteraction mi;
        bool scatter_surface = scene.Intersect(r, kShadowEpsilon, kInfinity, &si);
        float t_max = scatter_surface ? si.t : kInfinity;
        bool scatter_medium = false;

        // Local vertex segment tracking
        Spectrum current_beta = beta;  // Track beta before the bounce
        Spectrum local_vertex_L(0.0f);
        float vertex_alpha = 1.0f;

        if (r.vol_stack().GetActiveMedium() != 0) {
            scatter_medium = SampleMedium(r, scene, t_max, rng, beta, &mi);
        }
        // vol dispatch, sample medium with t_surface as upper bound
        if (scatter_medium) {
            ray_t += mi.t;

            /* Volume Next Event Estimation (Direct Lighting) */
            DirectLightSample dls;
            if (GenerateLightSample(mi.point, scene, rng, wl, &dls)) {
                Ray shadow_ray(mi.point, dls.wi);
                shadow_ray.vol_stack() = r.vol_stack();

                Spectrum Tr = EvaluateVisibility(scene, shadow_ray, dls.dist, rng, wl);

                if (Tr.MaxComponentValue() > 0.0f) {
                    // Evaluate Phase & Transmittance
                    float phase_val = EvalHG(mi.phase_g, mi.wo, dls.wi);
                    Spectrum direct_L = phase_val * Tr * dls.emission / dls.pdf;
                    local_vertex_L += direct_L;
                }
            }

            vertex_alpha = mi.alpha;

            L += current_beta * local_vertex_L;  // forward beauty accumulation

            dpr.AppendVertex(ray_t, ray_t, local_vertex_L, vertex_alpha, is_camera_path, true);
            is_camera_path = false;  // Volumes always scatter, leaving camera path

            /* Sample Phase Function for Indirect Bounce */
            Vec3 next_wi;
            SampleHG(mi.phase_g, mi.wo, rng.UniformFloat(), rng.UniformFloat(), next_wi);

            // Note: For Henyey-Greenstein, the phase_eval / phase_pdf ratio is EXACTLY 1.0
            // The sampling routine perfectly importance samples the distribution so beta is
            // unchanged by the directional scatter itself
            Ray next_r(mi.point, next_wi);
            next_r.vol_stack() = r.vol_stack();
            r = next_r;
            specular_bounce = false;
        } else if (scatter_surface) {
            ray_t += si.t;

            // TRANSPORT POLICY (Medium Transitions)
            if (si.interior_medium != si.exterior_medium) {
                float cos = Dot(r.direction(), si.n_geom);
                if (cos < 0.0f) {  // Entering the interior medium
                    if (si.interior_medium != kVacuumMediumId && si.interior_medium != 0) {
                        r.vol_stack().Push(si.interior_medium, si.priority);
                    }
                } else {  // Exiting the interior medium
                    if (si.interior_medium != kVacuumMediumId && si.interior_medium != 0) {
                        r.vol_stack().Pop(si.interior_medium);
                    }
                }
            }
            // SHADING POLICY (Opacity & BSDF)
            if (si.material_id == kNullMaterialId) {
                Ray next_ray(si.point + (r.direction() * kShadowEpsilon), r.direction());
                next_ray.vol_stack() = r.vol_stack();
                r = next_ray;
                depth--;
                continue;
            }

            const Material& mat = scene.GetMaterial(si.material_id);
            ShadingData sd = ResolveShadingData(mat, si, scene);

            // Lazy Evaluation
            Spectrum opacity(1.0f);
            float alpha = 1.0f;
            if (mat.IsTransparent() && mat.type != MaterialType::Dielectric) {
                opacity = CurveToSpectrum(mat.opacity, wl);
                alpha = opacity.Average();
            }

            Spectrum emission(0.0f);
            if (mat.IsEmissive()) {
                emission = CurveToSpectrum(mat.emission, wl);
                if (specular_bounce) {
                    local_vertex_L += emission;
                }
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
            if (mat.type != MaterialType::Metal && mat.type != MaterialType::Dielectric) {
                DirectLightSample dls;
                if (GenerateLightSample(si.point + (si.n_geom * kShadowEpsilon), scene, rng, wl,
                                        &dls)) {
                    Ray shadow_ray(si.point + (dls.wi * kShadowEpsilon), dls.wi);
                    shadow_ray.vol_stack() = r.vol_stack();

                    Spectrum Tr = EvaluateVisibility(scene, shadow_ray, dls.dist, rng, wl);

                    if (Tr.MaxComponentValue() > 0.0f) {
                        float cos_surf = std::fmax(0.0f, Dot(dls.wi, sd.n_shading));
                        Spectrum f_val = EvalBSDF(mat, sd, si.wo, dls.wi, wl);

                        Spectrum direct_L = f_val * Tr * dls.emission * cos_surf / dls.pdf;
                        direct_L *= opacity;
                        local_vertex_L += direct_L;
                    }
                }
            }

            vertex_alpha = alpha;
            L += current_beta * local_vertex_L;
            dpr.AppendVertex(ray_t, ray_t, local_vertex_L, vertex_alpha, is_camera_path, false);

            /* Indirect bounce case */
            Vec3 wi;
            float pdf;
            Spectrum f;

            /* BSDF check */
            if (SampleBSDF(mat, sd, r, si, rng, wl, wi, pdf, f)) {
                if (pdf > 0) {
                    float refract = Dot(wi, si.n_geom);
                    float cos_theta = std::abs(refract);
                    Spectrum weight = f * cos_theta / pdf;  // Universal pdf func now

                    // Modulate throughput by opacity for non-specular bounces
                    // For Dielectrics/Metals, opacity is typically 1.0
                    // For transparent diffuse, we need to account for absorption
                    if (mat.type == MaterialType::Lambertian) {
                        weight *= alpha;  // Absorb based on opacity
                    }

                    beta *= weight;
                    Ray next_r(si.point + (wi * kShadowEpsilon), wi);
                    next_r.vol_stack() = r.vol_stack();

                    // Update is_camera_path based on transmission
                    float in_dot = Dot(r.direction(), si.n_shading);
                    float out_dot = Dot(next_r.direction(), si.n_shading);
                    bool is_transmission = (in_dot * out_dot > 0.0f);

                    is_camera_path = is_camera_path && is_transmission;
                    r = next_r;

                    // If this bounce was sharp (Metal/Glass), next hit counts as specular
                    specular_bounce =
                        (mat.type == MaterialType::Metal || mat.type == MaterialType::Dielectric);
                }
            } else {
                break;
            }
        } else if (!scatter_surface) {
            Spectrum env_L = EvaluateEnvironment(r.direction(), wl);
            dpr.AppendVertex(kFarClip, kFarClip, env_L, 1.0f, is_camera_path, false);
            L += env_L * current_beta;
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

        dpr.UpdateBSDFWeight(beta, current_beta);
    }

    dpr.ResolveToDeep(result, ray, config.cam_w, wl);
    result.L = L;
    result.alpha = 1.0f;  // TODO: alpha accumulation in loop
    return result;
}

}  // namespace skwr

/**
 * conflicting:
 *
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

         //  Handle transparency - straight-through transmission */
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

// /* Next Event Estimation */
// if (mat.type != MaterialType::Metal && mat.type != MaterialType::Dielectric &&
//     !scene.Lights().empty()) {
//     int light_index = int(rng.UniformFloat() * scene.Lights().size());
//     const AreaLight& light = scene.Lights()[light_index];
//     LightSample ls = SampleLight(scene, light, rng);

//     // Shadow Ray setup
//     Vec3 to_light = ls.p - si.point;
//     float dist_sq = to_light.LengthSquared();
//     float dist = std::sqrt(dist_sq);
//     Vec3 wi_light = to_light / dist;

//     Ray shadow_ray(si.point + (wi_light * kShadowEpsilon), wi_light);
//     SurfaceInteraction shadow_si;  // dummy
//     if (!scene.Intersect(shadow_ray, 0.f, dist - 2.0f * kShadowEpsilon, &shadow_si)) {
//         float cos_light = std::abs(Dot(-wi_light, ls.n));
//         // Area PDF -> Solid Angle PDF: PDF_w = PDF_a * dist^2 / cos_light
//         if (cos_light > 0) {
//             float light_pdf_w = ls.pdf * dist_sq / cos_light;

//             // BSDF Evaluation
//             float cos_surf = std::fmax(0.0f, Dot(wi_light, sd.n_shading));
//             Spectrum f_val = EvalBSDF(mat, sd, si.wo, wi_light, wl);

//             // Accumulate
//             // Weight = 1.0 / (N_lights * PDF_w)
//             // L += beta * f * Le * cos_surf * Weight
//             Spectrum light_spec = CurveToSpectrum(ls.emission, wl);
//             Spectrum direct_L =
//                 beta * f_val * light_spec * cos_surf / (light_pdf_w * scene.InvLightCount());
//             direct_L *= opacity;
//             L += direct_L;
//         }
//     }
// }

// /* Indirect bounce case */
// Vec3 wi;
// float pdf;
// Spectrum f;

// /* BSDF check */
// if (SampleBSDF(mat, sd, r, si, rng, wl, wi, pdf, f)) {
//     if (pdf > 0) {
//         float refract = Dot(wi, si.n_geom);

//         if (!valid_deep_hit && counts_for_depth) {
//             if (!(refract < 0.0f)) {
//                 valid_deep_hit = true;  // it's a reflection
//             }
//         }

//         float cos_theta = std::abs(refract);
//         Spectrum weight = f * cos_theta / pdf;  // Universal pdf func now

//         // Modulate throughput by opacity for non-specular bounces
//         // For Dielectrics/Metals, opacity is typically 1.0
//         // For transparent diffuse, we need to account for absorption
//         if (mat.type == MaterialType::Lambertian) {
//             weight *= alpha;  // Absorb based on opacity
//         }

//         beta *= weight;
//         r = Ray(si.point + (wi * kShadowEpsilon), wi);

//         // If this bounce was sharp (Metal/Glass), next hit counts as specular
//         specular_bounce = (mat.type == MaterialType::Metal || mat.type ==
//         MaterialType::Dielectric);
//     }
// } else {
//     // Absorbed (black body)
//     break;
// }

// // Russian Roulette
// if (depth > 3) {
//     float max_beta = beta.MaxComponentValue();
//     if (max_beta < 0.001f) break;
//     float p = std::min(0.95f, max_beta);
//     if (rng.UniformFloat() > p) break;
//     beta = beta * (1.0f / p);
// }
// }

// RGB final_rgb = SpectrumToRGB(L, wl);

// // In transparent mode a pixel is "covered" only when a visible object was
// // hit within the visibility_depth window.  In opaque mode every geometry
// // hit counts (backward-compatible).
// const bool covered =
//     config.transparent_background ? camera_visible : (valid_deep_hit || camera_visible);
// result.alpha = covered ? 1.0f : (config.transparent_background ? 0.0f : 1.0f);

// // Deep segment: emit when we have a committed depth AND the pixel is covered.
// // In opaque mode the far-clip fallback ensures even pure misses are represented.
// if (valid_deep_hit && covered) {
//     Vec3 to_hit = deep_hit_point - deep_origin;
//     float z_depth = Dot(to_hit, config.cam_w);
//     if (z_depth < 0.0f) z_depth = 0.0f;
//     AddSegment(result, z_depth, z_depth + kShadowEpsilon, final_rgb, deep_hit_alpha);
// } else if (!config.transparent_background) {
//     // Opaque mode: solid far-clip sample so the deep pixel is always covered.
//     AddSegment(result, kFarClip, kFarClip + 1000.0f, final_rgb, deep_hit_alpha);
// }
// // transparent + !covered → no segments → compositor sees fully transparent pixel.

// result.L = L;
// return result;
// }
// */
