#ifndef SKWR_INTEGRATORS_INTEGRATOR_H_
#define SKWR_INTEGRATORS_INTEGRATOR_H_

namespace skwr {

class Scene;  // Forward declarations
class Film;

// Abstract base class (or just use a single PathTracer class for now)
class Integrator {
  public:
    virtual ~Integrator() = default;

    virtual void Render(const Scene& scene, Film* film) = 0;

  protected:
    // Calculates Radiance (Li) along a single ray
    // This is where your recursion/iteration happens!
    // virtual Spectrum Li(const Ray &ray, const Scene &scene, Sampler &sampler) const = 0;
};

}  // namespace skwr

#endif  // SKWR_INTEGRATORS_INTEGRATOR_H_
