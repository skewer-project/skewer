#include "integrators/path_trace.h"

#include <atomic>
#include <iomanip>
#include <thread>
#include <vector>

#include "barkeep.h"
#include "core/sampling.h"
#include "core/sampling/wavelength_sampler.h"
#include "core/spectrum.h"
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

    // Build tile list — tiles improve cache locality for BVH traversal and
    // film writes compared to the previous scanline-based work-stealing.
    int tile_size = config.tile_size;
    int tiles_x = (width + tile_size - 1) / tile_size;
    int tiles_y = (height + tile_size - 1) / tile_size;
    int total_tiles = tiles_x * tiles_y;

    std::cout << "[Session] Rendering with " << thread_count << " threads, " << tile_size << "x"
              << tile_size << " tiles (" << total_tiles << " total)...\n";

    std::atomic<int> next_tile(0);
    std::atomic<int> tiles_completed(0);

    auto bar = bk::ProgressBar(&tiles_completed, {
                                                     .total = total_tiles,
                                                     .speed = 0.2,
                                                     .speed_unit = "tiles/s",
                                                     .style = bk::ProgressBarStyle::Rich,
                                                 });

    const bool is_adaptive = config.noise_threshold > 0.0f;
    const int min_s = config.min_samples;
    const int step = config.adaptive_step;
    std::atomic<long long> total_samples_rendered(0);
    const long long max_possible_samples = (long long)width * height * config.max_samples;

    // Worker function — each thread grabs tiles dynamically
    auto render_worker = [&]() {
        while (true) {
            int tile_idx = next_tile.fetch_add(1);
            if (tile_idx >= total_tiles) break;

            int tile_col = tile_idx % tiles_x;
            int tile_row = tile_idx / tiles_x;
            int x0 = tile_col * tile_size;
            int y0 = tile_row * tile_size;
            int x1 = std::min(x0 + tile_size, width);
            int y1 = std::min(y0 + tile_size, height);

            long long tile_samples = 0;

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    RNG rng = MakeDeterministicPixelRNG(x, y, width, config.start_sample);

                    for (int s = 0; s < config.max_samples; ++s) {
                        float u = (float(x) + rng.UniformFloat()) / width;
                        float v = 1.0f - (float(y) + rng.UniformFloat()) / height;

                        SampledWavelengths wl = WavelengthSampler::Sample(rng.UniformFloat());

                        Ray r = cam.GetRay(u, v);

                        PathSample result = Li(r, scene, rng, config, wl);

                        RGB pixel_color = SpectrumToRGB(result.L, wl) * result.alpha;

                        if (is_adaptive) {
                            film->AddAdaptiveSample(x, y, pixel_color, result.alpha, 1.0f);

                            if (config.enable_deep) film->AddDeepSample(x, y, result);

                            // Check convergence periodically after min_samples
                            if (s + 1 >= min_s && ((s + 1 - min_s) % step == 0)) {
                                if (film->IsPixelConverged(x, y, config.noise_threshold)) {
                                    tile_samples += s + 1;
                                    goto next_pixel;
                                }
                            }
                        } else {
                            film->AddSample(x, y, pixel_color, result.alpha, 1.0f);

                            if (config.enable_deep) film->AddDeepSample(x, y, result);
                        }
                    }
                    tile_samples += config.max_samples;
                next_pixel:;
                }
            }

            total_samples_rendered.fetch_add(tile_samples);
            tiles_completed.fetch_add(1);
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

    if (is_adaptive) {
        long long rendered = total_samples_rendered.load();
        double pct_saved = 100.0 * (1.0 - (double)rendered / max_possible_samples);
        std::cout << "[Adaptive] " << rendered << " / " << max_possible_samples
                  << " samples rendered (" << std::fixed << std::setprecision(1) << pct_saved
                  << "% saved)\n";
    }
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
