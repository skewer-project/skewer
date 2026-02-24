#ifndef SKWR_INTEGRATORS_NORMALS_H_
#define SKWR_INTEGRATORS_NORMALS_H_

#include "integrators/integrator.h"

namespace skwr {

class Normals : public Integrator {
  public:
    void Render(const Scene& scene, const Camera& cam, Film* film,
                const IntegratorConfig& config) override;
};

}  // namespace skwr

#endif  // SKWR_INTEGRATORS_NORMALS_H_
