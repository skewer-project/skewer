#ifndef SKWR_INTEGRATORS_PATH_TRACER_H_
#define SKWR_INTEGRATORS_PATH_TRACER_H_

#include "integrators/integrator.h"

namespace skwr {

class PathIntegrator : public Integrator {
  public:
    void Render(const Scene& scene, Film* film) override;
};
}  // namespace skwr

#endif  // SKWR_INTEGRATORS_PATH_TRACER_H_
