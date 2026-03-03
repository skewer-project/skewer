#ifndef SKWR_KERNELS_UTILS_VISIBILITY_H_
#define SKWR_KERNELS_UTILS_VISIBILITY_H_

#include "core/spectral/spectrum.h"

namespace skwr {

class Scene;
class Ray;
class RNG;

Spectrum EvaluateVisibility(const Scene& scene, Ray& shadow_ray, float max_dist, RNG& rng,
                            const SampledWavelengths& wl);

}  // namespace skwr

#endif  // SKWR_KERNELS_UTILS_VISIBILITY_H_
