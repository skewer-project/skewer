#include "integrators/path_integrator.h"

#include "core/spectrum.h"
#include "film/film.h"
#include "scene/scene.h"

namespace skwr {

void PathIntegrator::Render(const Scene& scene, Film* film) {
    int width = film->width();
    int height = film->height();

    // move loop here from render
    // Eventually should be parallelized with std::<thread>
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Generate Ray
            // Ray ray = camera.GenerateRay(x, y);

            // Trace Ray
            // Spectrum L = Li(ray, scene);

            // Generating fake data
            float r = float(x) / (width - 1);
            float g = float(y) / (height - 1);
            float b = 0.25f;

            // Create a Spectrum (Color)
            Spectrum color(r, g, b);

            // Accumulate to Film
            // Note: We use AddSample, not SetPixel directly!
            film->AddSample(x, y, color, 1.0f);
        }
    }
}

}  // namespace skwr

// // Implement integrator abstract class.
// // Pseudocode trace
// Ray ray = camera.GenerateRay(pixel_x, pixel_y);

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
