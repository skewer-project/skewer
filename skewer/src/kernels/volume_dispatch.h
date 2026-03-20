#ifndef SKWR_KERNELS_VOLUME_DISPATCH_H_
#define SKWR_KERNELS_VOLUME_DISPATCH_H_

#include "core/spectral/spectrum.h"
#include "core/transport/medium_interaction.h"

namespace skwr {

class Scene;
class Ray;
class RNG;

bool SampleMedium(const Ray& ray, const Scene& scene, float t_max, RNG& rng, Spectrum& beta,
                  MediumInteraction* mi, const SampledWavelengths& wl);

}  // namespace skwr

#endif  // SKWR_KERNELS_VOLUME_DISPATCH_H_
