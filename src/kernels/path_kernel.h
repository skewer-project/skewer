#ifndef SKWR_KERNELS_PATH_KERNEL_H_
#define SKWR_KERNELS_PATH_KERNEL_H_

#include <cstdlib>

#include "core/color/color.h"
#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/medium_interaction.h"
#include "core/sampling/path_sample.h"
#include "core/sampling/rng.h"
#include "core/sampling/surface_interaction.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectral/spectrum.h"
#include "kernels/volume_dispatch.h"
#include "materials/bsdf.h"
#include "materials/material.h"
#include "materials/texture_lookup.h"
#include "scene/scene.h"
#include "session/render_options.h"

namespace skwr {

inline float CameraDepth(const Ray& ray, float t, const Vec3& cam_origin, const Vec3& cam_forward) {
    Vec3 o = ray.origin() - cam_origin;
    return Dot(o, cam_forward) + t * Dot(ray.direction(), cam_forward);
}

inline void AddDeepSegment(PathSample& sample, const Ray& ray, float t_min, float t_max,
                           const Spectrum& L, float alpha, const Vec3& cam_origin,
                           const Vec3& cam_forward, const SampledWavelengths& wl) {
    float z_front = CameraDepth(ray, t_min, cam_origin, cam_forward);

    float z_back = CameraDepth(ray, t_max, cam_origin, cam_forward);

    if (z_back < 0.0f) return;

    if (z_front < 0.0f) z_front = 0.0f;

    RGB final_rgb = SpectrumToRGB(L, wl);

    sample.segments.push_back({z_front, z_back, final_rgb, alpha});
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

    float ray_t = 0.0f;           // Running parametric distance
    float segment_start = ray_t;  // Deep interval start
    Spectrum segment_L(0.0f);

    // switch to while?
    for (int depth = 0; depth < config.max_depth; ++depth) {
        SurfaceInteraction si;
        MediumInteraction mi;
        bool scatterSurface = scene.Intersect(r, kShadowEpsilon, kInfinity, &si);  // y it pointer?
        float t_max = scatterSurface ? si.t : kInfinity;
        bool scatterMedium = false;

        if (r.vol_stack().GetActiveMedium() != 0) {
            scatterMedium = SampleMedium(r, scene, t_max, rng, beta, mi);
        }
        // vol dispatch, sample medium with t_surface as upper bound
        if (scatterMedium) {
            ray_t += mi.t;

            // Compute NEE for the volume point (mi) (in-scatter)
            // L += beta * EstimateDirect(mi, scene, rng, ...);

            // Sample Phase Function to get new ray direction
            // update beta with transmittance
            // beta *= phase_eval / phase_pdf;
            if (specular_bounce) {
                // AddSegment(result, interval_start, mi.t, L, interval_opacity);
                specular_bounce = false;
            }
            segment_start = ray_t;
            L += segment_L;
            segment_L = Spectrum(0.0f);
            // r = Ray(mi.point, new_dir);
            specular_bounce = false;
            // interval_start = mi.t;
            // r = Ray(mi.point, new_dir);
            // r.vol_stack() remains unchanged
            specular_bounce = false;
        }
        if (scatterSurface) {
            ray_t += si.t;
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
                    segment_L += beta * emission;
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
                        // interval_L += T * L_scatter;
                        direct_L *= opacity;
                        segment_L += direct_L;
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
                    float cos_theta = std::abs(refract);
                    Spectrum weight = f * cos_theta / pdf;  // Universal pdf func now

                    // Modulate throughput by opacity for non-specular bounces
                    // For Dielectrics/Metals, opacity is typically 1.0
                    // For transparent diffuse, we need to account for absorption
                    if (mat.type == MaterialType::Lambertian) {
                        weight *= alpha;  // Absorb based on opacity
                    }

                    beta *= weight;
                    if (specular_bounce) {
                        AddDeepSegment(result, r, segment_start, ray_t, segment_L, 1.0f, r.origin(),
                                       config.cam_w, wl);
                    }
                    segment_start = ray_t;
                    L += segment_L;
                    segment_L = Spectrum(0.0f);

                    Ray next_ray(si.point + (wi * kShadowEpsilon), wi);
                    next_ray.vol_stack() = r.vol_stack();

                    // Check for Transmission (did it cross the boundary?)
                    // si.wo points towards the camera. wi points towards the next bounce.
                    float to_cam = Dot(si.wo, si.n_geom);
                    bool is_transmission = (to_cam * refract < 0.0f);

                    if (is_transmission) {
                        // Check if we hit the outside face (entering) or inside face (exiting)
                        bool hit_outside = to_cam > 0.0f;

                        if (hit_outside) {
                            // Entering the object. Push the object's interior medium.
                            // Note: You need to fetch this from the material or shape!
                            uint16_t interior_medium = si.interior_medium;
                            if (interior_medium != 0) {
                                next_ray.vol_stack().Push(interior_medium, si.priority);
                            }
                        } else {
                            // Exiting the object. Pop the current medium.
                            next_ray.vol_stack().Pop(next_ray.vol_stack().GetActiveMedium());
                        }

                        // Note: When transmitting, we offset the ray origin to the *other* side
                        // of the surface to avoid self-intersection with the interior face.
                        r = Ray(si.point - (si.n_geom * kShadowEpsilon), wi);
                    } else {
                        // Standard reflection.
                        r = Ray(si.point + (si.n_geom * kShadowEpsilon), wi);
                    }

                    // If this bounce was sharp (Metal/Glass), next hit counts as specular
                    specular_bounce =
                        (mat.type == MaterialType::Metal || mat.type == MaterialType::Dielectric);
                }
            } else {
                // Absorbed (black body)
                break;
            }
        }
        if (!scatterSurface) {
            // Environment Segment
            Spectrum env_L = beta * Spectrum(0.0f);
            segment_L += env_L;
            if (specular_bounce) {
                AddDeepSegment(result, r, segment_start, kFarClip, segment_L, 1.0f, r.origin(),
                               config.cam_w, wl);
            }
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

    result.L = L;
    return result;
}

}  // namespace skwr

#endif  // SKWR_KERNELS_PATH_KERNEL_H_

/**
 * RGB final_rgb = SpectrumToRGB(L, wl);
 if (valid_deep_hit) {
     AddSegment(result, z_depth, z_depth + kShadowEpsilon, final_rgb, deep_hit_alpha);
 } else {
     AddSegment(result, kFarClip, kFarClip + 1000.0f, final_rgb, deep_hit_alpha);
 }
 */

// inline void AddSegment(PathSample& sample, const float& t_min, const float& t_max,
//                        const Spectrum& L, const float& alpha, const SampledWavelengths &wl) {
//     float z_depth = Dot(deep_hit_point - deep_origin, cam_w);
//     // Ensure we don't get negative depth behind camera
//     if (z_depth < 0.0f) z_depth = 0.0f;
//     RGB final_rgb = SpectrumToRGB(L, wl)
//     AddSegment(result, z_depth, z_depth + kShadowEpsilon, final_rgb, deep_hit_alpha);
//     sample.segments.push_back({t_min, t_max, L, alpha});
// }

// Spectrum T(1.0f);              // running transmittance
// Spectrum interval_L(0.0f);     // accumulated in-scatter over interval

// // Deep Info
// bool valid_deep_hit = false;
// Point3 deep_hit_point = r.at(kFarClip);

// float deep_hit_alpha = 1.0f;  // default solid
