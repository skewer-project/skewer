#include "integrators/path_trace.h"

#include <algorithm>

#include "core/constants.h"
#include "core/sampling.h"
#include "core/spectrum.h"
#include "core/vec3.h"
#include "film/film.h"
#include "scene/camera.h"
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
void PathTrace::Render(const Scene &scene, const Camera &cam, Film *film,
                       const IntegratorConfig &config) {
    int width = film->width();
    int height = film->height();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int s = 0; s < config.samples_per_pixel; ++s) {
                RNG rng = MakeDeterministicPixelRNG(x, y, width, s);
                Float u = (Float(x) + rng.UniformFloat()) / width;
                Float v = (Float(y) + rng.UniformFloat()) / height;

                Ray r = cam.GetRay(u, v);
                SurfaceInteraction si;
                const Float t_min = 0.001f;
                Spectrum L(0.0f);     // Accumulated Radiance (color)
                Spectrum beta(1.0f);  // Throughput (attenuation)

                // "Bounce" loop - iterative not recursive tho
                // RN, this is calculating Li: how much Radiance (L) is incoming (i)
                // And it does that by multiplying the total light by the amount lost at the end
                for (int depth = 0; depth < config.max_depth; ++depth) {
                    if (!scene.Intersect(r, t_min, kInfinity, &si)) {
                        // if we dont hit anything, sky color
                        Spectrum sky_color(0.5f, 0.7f, 1.0f);
                        L += beta * sky_color;  // <-- beta was 1 but by this point, is a fraction
                        break;
                    }

                    /* Emission check for if we hit a light */

                    /* Scattering check */
                    // We assuming everything is lambertian (diffuse)
                    // PDF of cosine sampling (top of sphere?) = cos(theta) / PI

                    Vec3 scatter_direction = si.n + RandomUnitVector(rng);

                    // degenerate scatter direction case
                    if (scatter_direction.Near_zero()) scatter_direction = si.n;

                    // Update Beta
                    // Monte Carlo integration for sampling..
                    // For Lambertian: f = Albedo / PI
                    // PDF = Cos / PI
                    // Update = (f * Cos) / PDF  ---> (Albedo/PI * Cos) / (Cos/PI) = Albedo
                    // So for pure random diffuse, we just multiply by Albedo.

                    Spectrum albedo(0.5f);  // gray for now
                    beta *= albedo;

                    r = Ray(si.p + (si.n * 0.001f), Normalize(scatter_direction));

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
