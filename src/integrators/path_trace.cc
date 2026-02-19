#include "integrators/path_trace.h"

#include <atomic>
#include <thread>
#include <vector>

#include "barkeep.h"
#include "core/sampling.h"
#include "film/film.h"
#include "integrators/path_sample.h"
#include "kernels/path_kernel.h"
#include "scene/camera.h"
#include "scene/light.h"
#include "scene/scene.h"
#include "session/render_options.h"

namespace bk = barkeep;

namespace skwr {

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

    auto bar = bk::ProgressBar(&scanlines_completed, {
                                                         .total = height,
                                                         .message = "Rendering",
                                                         .speed = 0.0,
                                                         .speed_unit = "scanlines/s",
                                                         .style = bk::ProgressBarStyle::Line,
                                                     });

    // Worker function - each thread grabs scanlines dynamically
    auto render_worker = [&]() {
        while (true) {
            int y = next_scanline.fetch_add(1);
            if (y >= height) break;

            std::clog.flush();
            for (int x = 0; x < width; ++x) {
                for (int s = 0; s < config.samples_per_pixel; ++s) {
                    RNG rng = MakeDeterministicPixelRNG(x, y, width, s);
                    float u = (float(x) + rng.UniformFloat()) / width;
                    float v = 1.0f - (float(y) + rng.UniformFloat()) / height;

                    Ray r = cam.GetRay(u, v);

                    PathSample result = Li(r, scene, rng, config);

                    float weight = 1.0f;
                    film->AddSample(x, y, result.L, weight);

                    if (config.enable_deep) film->AddDeepSample(x, y, result, weight);
                }
            }

            int done = scanlines_completed.fetch_add(1) + 1;
        }
    };

    bar->show();

    // Launch worker threads
    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back(render_worker);
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    bar->done();

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
