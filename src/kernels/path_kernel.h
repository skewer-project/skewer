#include <algorithm>

#include "core/constants.h"
#include "core/ray.h"
#include "core/rng.h"
#include "core/spectrum.h"
#include "core/vec3.h"
#include "materials/bsdf.h"
#include "materials/material.h"
#include "scene/scene.h"
#include "scene/surface_interaction.h"
#include "session/render_options.h"

namespace skwr {

inline Spectrum Li(const Ray& ray, const Scene& scene, RNG& rng, const IntegratorConfig& config) {
    Spectrum L(0.0f);     // Accumulated Radiance (color)
    Spectrum beta(1.0f);  // Throughput (attenuation)
    Ray r = ray;
    bool specular_bounce = true;

    // "Bounce" loop - calculates Li: how much Radiance (L) is incoming (i)
    // by multiplying the total light by the amount lost at the end
    for (int depth = 0; depth < config.max_depth; ++depth) {
        SurfaceInteraction si;
        if (!scene.Intersect(r, kShadowEpsilon, kInfinity, &si)) {
            L += beta * Spectrum(0.0f);
            break;
        }

        const Material& mat = scene.GetMaterial(si.material_id);

        /* Emission check for if we hit a light */
        if (mat.IsEmissive()) {
            if (specular_bounce) {
                L += beta * mat.emission;
            }
        }

        /* Next Event Estimation */
        if (mat.type != MaterialType::Metal && mat.type != MaterialType::Dielectric &&
            !scene.Lights().empty()) {
            int light_index = int(rng.UniformFloat() * scene.Lights().size());
            const AreaLight& light = scene.Lights()[light_index];
            LightSample ls = Sample_Light(scene, light, rng);

            // Shadow Ray setup
            Vec3 to_light = ls.p - si.p;
            Float dist_sq = to_light.LengthSquared();
            Float dist = std::sqrt(dist_sq);
            Vec3 wi_light = to_light / dist;

            Ray shadow_ray(si.p + (wi_light * kShadowEpsilon), wi_light);
            SurfaceInteraction shadow_si;  // dummy
            if (!scene.Intersect(shadow_ray, 0.f, dist - kShadowEpsilon, &shadow_si)) {
                Float cos_light = std::fmax(0.0f, Dot(-wi_light, ls.n));
                // Area PDF -> Solid Angle PDF: PDF_w = PDF_a * dist^2 / cos_light
                if (cos_light > 0) {
                    Float light_pdf_w = ls.pdf * dist_sq / cos_light;

                    // BSDF Evaluation
                    Float cos_surf = std::fmax(0.0f, Dot(wi_light, si.n));
                    Spectrum f_val = Eval_BSDF(mat, si.wo, wi_light, si.n);

                    // Accumulate
                    // Weight = 1.0 / (N_lights * PDF_w)
                    // L += beta * f * Le * cos_surf * Weight
                    Float selection_prob = 1.0f / scene.Lights().size();
                    L += beta * f_val * ls.emission * cos_surf / (light_pdf_w * selection_prob);
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
                Float cos_theta = std::abs(Dot(wi, si.n));
                Spectrum weight = f * cos_theta / pdf;  // Universal pdf func now
                beta *= weight;
                r = Ray(si.p + (wi * kShadowEpsilon), wi);

                // If this bounce was sharp (Metal/Glass), next hit counts as
                // specular
                specular_bounce =
                    (mat.type == MaterialType::Metal || mat.type == MaterialType::Dielectric);
            }
        } else {
            // Absorbed (black body)
            break;
        }

        // Russian Roulette method to kill weak rays early
        // is an optimization cause weak rays = weak influence on final
        if (depth > 3) {
            Float p = std::max(beta.r(), std::max(beta.g(), beta.b()));
            if (rng.UniformFloat() > p) break;
            beta = beta * (1.0f / p);
        }
    }
    return L;
}

}  // namespace skwr
