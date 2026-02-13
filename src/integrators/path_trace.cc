#include "integrators/path_trace.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "core/constants.h"
#include "core/sampling.h"
#include "core/spectrum.h"
#include "core/vec3.h"
#include "film/film.h"
#include "materials/bsdf.h"
#include "materials/material.h"
#include "scene/camera.h"
#include "scene/light.h"
#include "scene/scene.h"
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
void PathTrace::Render(const Scene& scene, const Camera& cam, Film* film,
                       const IntegratorConfig& config) {
    int width = film->width();
    int height = film->height();

    // Determine number of threads
    int thread_count = config.num_threads;
    if (thread_count <= 0) {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 4;  // Fallback
    }

    std::clog << "[Session] Rendering with " << thread_count << " threads...\n";

    // Atomic counter for scanline work-stealing
    std::atomic<int> next_scanline(0);
    std::atomic<int> scanlines_completed(0);
    std::mutex progress_mutex;

    // Worker function - each thread grabs scanlines dynamically
    auto render_worker = [&]() {
        while (true) {
            int y = next_scanline.fetch_add(1);
            if (y >= height) break;

            std::clog.flush();
            for (int x = 0; x < width; ++x) {
                for (int s = 0; s < config.samples_per_pixel; ++s) {
                    RNG rng = MakeDeterministicPixelRNG(x, y, width, s);
                    Float u = (Float(x) + rng.UniformFloat()) / width;
                    Float v = 1.0f - (Float(y) + rng.UniformFloat()) / height;

                    Ray r = cam.GetRay(u, v);
                    SurfaceInteraction si;
                    const Float t_min = kShadowEpsilon;
                    Spectrum L(0.0f);     // Accumulated Radiance (color)
                    Spectrum beta(1.0f);  // Throughput (attenuation)
                    bool specular_bounce = true;

                    // "Bounce" loop - iterative not recursive tho
                    // RN, this is calculating Li: how much Radiance (L) is incoming (i)
                    // And it does that by multiplying the total light by the amount lost at the end
                    for (int depth = 0; depth < config.max_depth; ++depth) {
                        if (!scene.Intersect(r, t_min, kInfinity, &si)) {
                            // if we dont hit anything, sky color
                            // Spectrum sky_color(0.5f, 0.7f, 1.0f);
                            Spectrum sky_color(0.f, 0.f, 0.f);
                            L += beta *
                                 sky_color;  // <-- beta was 1 but by this point, is a fraction
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
                        if (mat.type != MaterialType::Metal &&
                            mat.type != MaterialType::Dielectric && !scene.Lights().empty()) {
                            int light_index = int(rng.UniformFloat() * scene.Lights().size());
                            const AreaLight& light = scene.Lights()[light_index];
                            LightSample ls = SampleLight(scene, light, rng);

                            // Shadow Ray setup
                            Vec3 to_light = ls.p - si.p;
                            Float dist_sq = to_light.LengthSquared();
                            Float dist = std::sqrt(dist_sq);
                            Vec3 wi_light = to_light / dist;

                            Ray shadow_ray(si.p + (wi_light * kShadowEpsilon), wi_light);
                            SurfaceInteraction shadow_si;  // dummy
                            if (!scene.Intersect(shadow_ray, 0.f, dist - kShadowEpsilon,
                                                 &shadow_si)) {
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
                                    L += beta * f_val * ls.emission * cos_surf /
                                         (light_pdf_w * selection_prob);
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
                                specular_bounce = (mat.type == MaterialType::Metal ||
                                                   mat.type == MaterialType::Dielectric);
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
                    // Accumulate to Film
                    // Note: We use AddSample, not SetPixel directly!
                    film->AddSample(x, y, L, 1.0f);
                }
            }

            int done = scanlines_completed.fetch_add(1) + 1;
            std::lock_guard<std::mutex> lock(progress_mutex);
            std::clog << "[Session] Scanlines: " << done << " / " << height << "\t\r" << std::flush;
        }
    };

    // Launch worker threads
    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back(render_worker);
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    std::clog << "\n";
}

}  // namespace skwr

// // Step A: Traversal (The Accelerator)
// Interaction isect;
// if (scene.bvh.Intersect(ray, &isect)) {
//     // Step B: Shading (The Material)
//     // The Integrator looks at isect.material_id
//     // It calls material.Eval() to get color/physics

//     // Step C: Deep Recording (The Film)
//     if (collecting_deep) {
//         film.AddDeepSample(pixel_index, ray.t, isect.color);
//     }
// }

// The Main Loop:
// 1. Iterate over image tiles
// 2. Generate Rays (Camera::GenerateRay)
// 3. Trace Rays (Integrator::Li)
// 4. Store Result (Film::AddSample)
/*
 * // The Integrator (replaces color function)
 void RenderTile(Tile& tile, Scene& scene) {
     for (int pixel : tile) {
         Ray ray = camera.GenerateRay(pixel);
         Spectrum throughput = 1.0;

         // ITERATIVE LOOP (Not Recursive)
         for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {

             // 1. Intersect
             Intersection isect = scene.Intersect(ray);
             if (!isect.hit) {
                 pixel.AddColor(throughput * scene.Background(ray));
  // Record the hit depth and the current accumulated throughput
         film.AddDeepSample(pixelID, ray.t, throughput, accumulatedColor);
                 break;
             }

             // 2. Volume Handling (Deep Image support goes here)
             if (ray.in_medium) {
                 throughput *= SampleMedium(ray, isect);
 // For volumes, you might record samples at regular intervals
         // or at density boundaries (transmittance changes).
         film.AddVolumetricSample(pixelID, ray.t_min, ray.t_max, density);
             }

             // 3. Material Properties (Data lookup, not virtual call)
             Bsdf bsdf = scene.GetBSDF(isect.material_id, isect.uv);

             // 4. Light Sampling (Next Event Estimation)
             // Explicitly connect to a light source (reduces noise)
             pixel.AddColor(throughput * SampleOneLight(scene, isect, bsdf));

             // 5. Next Ray
             // Integrator decides the next ray, NOT the material
             ray = bsdf.SampleNextRay(ray);
             throughput *= bsdf.eval();
         }
     }
     }
     */
