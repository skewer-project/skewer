#ifndef SKWR_KERNELS_PATH_KERNEL_H_
#define SKWR_KERNELS_PATH_KERNEL_H_

#include "core/spectral/spectrum.h"
#include "film/sample_writer.h"

namespace skwr {

class Scene;
class Ray;
class RNG;
struct IntegratorConfig;

void Li(const Ray& ray, const Scene& scene, RNG& rng, const IntegratorConfig& config,
        const SampledWavelengths& wl, SampleWriter& writer);

}  // namespace skwr

#endif  // SKWR_KERNELS_PATH_KERNEL_H_
