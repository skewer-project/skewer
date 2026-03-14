#ifndef SKWR_KERNELS_PATH_KERNEL_H_
#define SKWR_KERNELS_PATH_KERNEL_H_

#include "core/spectral/spectrum.h"
#include "core/transport/path_sample.h"

namespace skwr {

class Scene;
class Ray;
class RNG;
struct IntegratorConfig;

PathSample Li(const Ray& ray, const Scene& scene, RNG& rng, const IntegratorConfig& config,
              const SampledWavelengths& wl);

}  // namespace skwr

#endif  // SKWR_KERNELS_PATH_KERNEL_H_
