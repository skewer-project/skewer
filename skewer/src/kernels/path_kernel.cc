#include "kernels/path_kernel.h"

#include <cstdlib>

#include "core/color/color.h"
#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/medium_interaction.h"
#include "core/sampling/path_sample.h"
#include "core/sampling/rng.h"
#include "core/sampling/surface_interaction.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectral/spectrum.h"
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

// struct for Deferred Backwards Pass for Deep output
// TODO: move to new file possibly
struct PathVertex {
    float t_start;
    float t_end;
    Spectrum local_L;                       // Local NEE + Emission ONLY (Unattenuated)
    Spectrum bsdf_weight = Spectrum(1.0f);  // The local BSDF or Phase throughput (f * cos / pdf)
    float alpha;                            // Opacity of the surface or 1.0 - Tr for volumes
    bool is_camera_path;                    // Should this vertex become a deep segment?
    bool is_volume_scatter = false;
};

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
PathSample Li(const Ray& ray, const Scene& scene, RNG& rng, const IntegratorConfig& config,
              const SampledWavelengths& wl) {
    // disgusting temp helper
    auto SafeSpectralDiv = [](const Spectrum& num, const Spectrum& den) -> Spectrum {
        Spectrum result(0.0f);
        for (int i = 0; i < kNSamples; ++i) {
            if (den[i] > 1e-8f) {  // Protect against exact 0 and denormals
                result[i] = num[i] / den[i];
            }
        }
        return result;
    };

    PathSample result;
    Spectrum L(0.0f);     // Accumulated Radiance (color)
    Spectrum beta(1.0f);  // Throughput (attenuation)
    Ray r = ray;
    bool specular_bounce = true;

    float ray_t = 0.0f;           // Running parametric distance
    float segment_start = ray_t;  // Deep interval start

    // Deferred State
    std::vector<PathVertex> path_vertices;
    path_vertices.reserve(config.max_depth);
    bool is_camera_path = true;

    // switch to while?
    for (int depth = 0; depth < config.max_depth; ++depth) {
        SurfaceInteraction si;
        MediumInteraction mi;
        bool scatterSurface = scene.Intersect(r, kShadowEpsilon, kInfinity, &si);  // y it pointer?
        float t_max = scatterSurface ? si.t : kInfinity;
        bool scatterMedium = false;

        // Local vertex segment tracking
        Spectrum current_beta = beta;  // Track beta before the bounce
        Spectrum local_vertex_L(0.0f);
        float vertex_alpha = 1.0f;

        if (r.vol_stack().GetActiveMedium() != 0) {
            scatterMedium = SampleMedium(r, scene, t_max, rng, beta, mi);
        }
        // vol dispatch, sample medium with t_surface as upper bound
        if (scatterMedium) {
            ray_t += mi.t;

            /* Volume Next Event Estimation (Direct Lighting) */
            if (!scene.Lights().empty()) {
                int light_index = int(rng.UniformFloat() * scene.Lights().size());
                const AreaLight& light = scene.Lights()[light_index];
                LightSample ls = SampleLight(scene, light, rng);

                Vec3 to_light = ls.p - mi.point;
                float dist_sq = to_light.LengthSquared();
                float dist = std::sqrt(dist_sq);
                Vec3 wi_light = to_light / dist;

                // Setup shadow ray
                Ray shadow_ray(mi.point, wi_light);
                shadow_ray.vol_stack() = r.vol_stack();

                Spectrum Tr = EvaluateVisibility(scene, shadow_ray, dist, rng, wl);

                if (Tr.MaxComponentValue() > 0.0f) {
                    float cos_light = std::fmax(0.0f, Dot(-wi_light, ls.n));
                    if (cos_light > 0) {
                        float light_pdf_w = ls.pdf * dist_sq / cos_light;

                        // Evaluate Phase & Transmittance
                        float phase_val = EvalHG(mi.phase_g, mi.wo, wi_light);
                        Spectrum light_spec = CurveToSpectrum(ls.emission, wl);

                        // Accumulate
                        Spectrum direct_L =
                            phase_val * Tr * light_spec / (light_pdf_w * scene.InvLightCount());
                        local_vertex_L += direct_L;
                    }
                }
            }
            vertex_alpha = mi.alpha;

            L += current_beta * local_vertex_L;  // forward beauty accumulation

            PathVertex v;
            v.t_start = segment_start;
            v.t_end = ray_t;
            v.local_L = local_vertex_L;
            v.alpha = vertex_alpha;
            v.is_camera_path = is_camera_path;
            v.is_volume_scatter = true;
            // We calculate weight AFTER the bounce/RR, so we just push the vertex
            // now and update its weight at the end of the loop iteration.
            path_vertices.push_back(v);

            is_camera_path = false;  // Volumes always scatter, leaving camera path
            segment_start = ray_t;

            /* 3. Sample Phase Function for Indirect Bounce */
            Vec3 next_wi;
            SampleHG(mi.phase_g, mi.wo, rng.UniformFloat(), rng.UniformFloat(), next_wi);

            // Note: For Henyey-Greenstein, the phase_eval / phase_pdf ratio is EXACTLY 1.0
            // The sampling routine perfectly importance samples the distribution so beta is
            // unchanged by the directional scatter itself
            Ray next_r(mi.point, next_wi);
            next_r.vol_stack() = r.vol_stack();
            r = next_r;
            specular_bounce = false;

        } else if (scatterSurface) {
            ray_t += si.t;

            // ==========================================
            // TRANSPORT POLICY (Medium Transitions)
            // ==========================================
            if (si.interior_medium != si.exterior_medium) {
                float cos = Dot(r.direction(), si.n_geom);
                if (cos < 0.0f) {
                    // Entering the interior medium
                    if (si.interior_medium != kVacuumMediumId && si.interior_medium != 0) {
                        r.vol_stack().Push(si.interior_medium, si.priority);
                    }
                } else {
                    // Exiting the interior medium
                    if (si.interior_medium != kVacuumMediumId && si.interior_medium != 0) {
                        r.vol_stack().Pop(si.interior_medium);
                    }
                }
            }
            // ==========================================
            // SHADING POLICY (Opacity & BSDF)
            // ==========================================
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
                shadow_ray.vol_stack() = r.vol_stack();

                Spectrum Tr = EvaluateVisibility(scene, shadow_ray, dist, rng, wl);

                if (Tr.MaxComponentValue() > 0.0f) {
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
                        Spectrum direct_L = f_val * Tr * light_spec * cos_surf /
                                            (light_pdf_w * scene.InvLightCount());
                        // interval_L += T * L_scatter;
                        direct_L *= opacity;
                        local_vertex_L += direct_L;
                    }
                }
            }
            vertex_alpha = alpha;

            L += current_beta * local_vertex_L;

            PathVertex v;
            v.t_start = ray_t;
            v.t_end = ray_t;
            v.local_L = local_vertex_L;
            v.alpha = vertex_alpha;
            v.is_camera_path = is_camera_path;
            path_vertices.push_back(v);

            segment_start = ray_t;

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
        } else if (!scatterSurface) {
            // Environment Segment
            Spectrum env_L = EvaluateEnvironment(r.direction(), wl);

            PathVertex v;
            v.t_start = kFarClip;
            v.t_end = kFarClip;
            v.local_L = env_L;
            v.alpha = 1.0f;
            v.is_camera_path = is_camera_path;
            path_vertices.push_back(v);

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

        if (!path_vertices.empty()) {
            // SafeSpectralDiv cleanly extracts the (f * cos / pdf * RR) weight
            path_vertices.back().bsdf_weight = SafeSpectralDiv(beta, current_beta);
        }
    }

    Spectrum deep_L(0.0f);

    // Iterate backwards from the end of the path to the camera
    for (int i = (int)path_vertices.size() - 1; i >= 0; --i) {
        const PathVertex& v = path_vertices[i];

        // Rendering Equation: L_out = L_local + weight * L_incoming
        deep_L = v.local_L + (v.bsdf_weight * deep_L);

        if (v.is_camera_path) {
            float deep_alpha = v.alpha;
            // FIX: If the NEXT vertex left the camera path, then THIS vertex is the
            // deflection point (scattering event or reflection). It acts as an opaque
            // terminator for this specific Monte Carlo sample's line of sight
            if (i + 1 < (int)path_vertices.size() && !path_vertices[i + 1].is_camera_path) {
                deep_alpha = 1.0f;
            }

            // FIX: Premature Termination Backstop
            // If this is the absolute end of the traced path, but we are STILL on the
            // camera path, it means the path was killed (RR, Max Depth) before hitting
            // an opaque background. Seal the alpha to prevent checkerboard bleeding
            if (i + 1 == (int)path_vertices.size()) {
                if (!v.is_volume_scatter) {
                    deep_alpha = 1.0f;
                }
            }

            // Segment with its own emission, NEE, and all indirect GI for this specific depth.
            AddDeepSegment(result, ray, v.t_start, v.t_end, deep_L, deep_alpha, ray.origin(),
                           config.cam_w, wl);

            // Deep Compositing Reset
            // Because the deep compositing software uses v.alpha to blend
            // whatever is behind this segment, we MUST reset the accumulator to 0.0f here.
            // Otherwise, the background light will be embedded in the foreground segment,
            // and Nuke will composite the background over itself twice.
            deep_L = Spectrum(0.0f);
        }
    }

    result.L = L;
    return result;
}

}  // namespace skwr

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

// if (scatterMedium) {
//     ray_t += mi.t;

//     // Compute NEE for the volume point (mi) (in-scatter)
//     // L += beta * EstimateDirect(mi, scene, rng, ...);

//     // Sample Phase Function to get new ray direction
//     // update beta with transmittance
//     // beta *= phase_eval / phase_pdf;
//     if (specular_bounce) {
//         // AddSegment(result, interval_start, mi.t, L, interval_opacity);
//         specular_bounce = false;
//     }
//     segment_start = ray_t;
//     L += segment_L;
//     segment_L = Spectrum(0.0f);
//     // r = Ray(mi.point, new_dir);
//     specular_bounce = false;
//     // interval_start = mi.t;
//     // r = Ray(mi.point, new_dir);
//     // r.vol_stack() remains unchanged
//     specular_bounce = false;
// }
