#ifndef SKWR_KERNELS_SAMPLE_HOMOGENEOUS_H_
#define SKWR_KERNELS_SAMPLE_HOMOGENEOUS_H_

#include "core/ray.h"
#include "core/sampling/medium_interaction.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectrum.h"
#include "media/mediums.h"

namespace skwr {

inline bool SampleHomogeneous(const HomogeneousMedium& medium, const Ray& r, float t_max, RNG& rng,
                              Spectrum& beta, MediumInteraction& mi) {
    return true;
}

}  // namespace skwr

#endif  // SKWR_KERNELS_SAMPLE_HOMOGENEOUS_H_
