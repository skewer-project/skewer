#include "integrators/path_trace.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "core/constants.h"
#include "core/sampling.h"
#include "core/spectrum.h"
#include "film/film.h"
#include "materials/material.h"
#include "scene/camera.h"
#include "scene/material_scatter.h"
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

                    // "Bounce" loop - iterative not recursive tho
                    for (int depth = 0; depth < config.max_depth; ++depth) {
                        if (!scene.Intersect(r, t_min, kInfinity, &si)) {
                            Spectrum sky_color(0.5f, 0.7f, 1.0f);
                            L += beta * sky_color;
                            break;
                        }

                        const Material& mat = scene.GetMaterial(si.material_id);

                        Spectrum attenuation;
                        Ray scattered_ray;

                        if (Scatter(mat, r, si, rng, attenuation, scattered_ray)) {
                            beta *= attenuation;
                            r = scattered_ray;
                        } else {
                            break;
                        }

                        // Russian Roulette
                        if (depth > 3) {
                            Float p = std::max(beta.r(), std::max(beta.g(), beta.b()));
                            if (rng.UniformFloat() > p) break;
                            beta = beta * (1.0f / p);
                        }
                    }
                    film->AddSample(x, y, L, 1.0f);
                }
            }

            int done = scanlines_completed.fetch_add(1) + 1;
            std::lock_guard<std::mutex> lock(progress_mutex);
            std::clog << "[Session] Scanlines: " << done << " / " << height << "\t\r"
                      << std::flush;
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
