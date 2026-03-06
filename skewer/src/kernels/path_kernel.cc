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
    PathSample result;
    Spectrum L(0.0f);     // Accumulated Radiance (color)
    Spectrum beta(1.0f);  // Throughput (attenuation)
    Ray r = ray;
    bool specular_bounce = true;

    float ray_t = 0.0f;  // Running parametric distance

    DeepPathRecorder dpr(config.max_depth);  // Deferred State tracker

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

            dpr.AppendVertex(ray_t, ray_t, local_vertex_L, vertex_alpha, is_camera_path, true);
            is_camera_path = false;  // Volumes always scatter, leaving camera path

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
        } else if (!scatterSurface) {
            // Environment Segment
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
