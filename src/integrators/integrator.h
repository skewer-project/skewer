#ifndef SKWR_INTEGRATORS_INTEGRATOR_H_
#define SKWR_INTEGRATORS_INTEGRATOR_H_

#include "film/film.h"
#include "scene/camera.h"
#include "scene/scene.h"

namespace skwr {

// Abstract base class (or just use a single PathTracer class for now)
class Integrator {
  public:
    virtual ~Integrator() = default;

    // The Main Loop:
    // 1. Iterate over image tiles
    // 2. Generate Rays (Camera::GenerateRay)
    // 3. Trace Rays (Integrator::Li)
    // 4. Store Result (Film::AddSample)
    virtual void Render(const Scene &scene, Film *film) = 0;

  protected:
    // Calculates Radiance (Li) along a single ray
    // This is where your recursion/iteration happens!
    virtual Spectrum Li(const Ray &ray, const Scene &scene, Sampler &sampler) const = 0;
};

}  // namespace skwr

#endif  // SKWR_INTEGRATORS_INTEGRATOR_H_
