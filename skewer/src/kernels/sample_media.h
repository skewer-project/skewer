#ifndef SKWR_KERNELS_SAMPLE_MEDIA_H_
#define SKWR_KERNELS_SAMPLE_MEDIA_H_

#include "core/spectral/spectrum.h"
#include "core/transport/medium_interaction.h"
#include "media/nano_vdb_medium.h"

namespace skwr {

struct HomogeneousMedium;
struct GridMedium;
class Ray;
class RNG;

bool SampleHomogeneous(const HomogeneousMedium& medium, const Ray& r, float t_max, RNG& rng,
                       Spectrum& beta, MediumInteraction* mi);
bool SampleGrid(const GridMedium& medium, const Ray& r, float t_max_surface, RNG& rng,
                Spectrum& beta, MediumInteraction* mi);
bool SampleNanoVDB(const NanoVDBMedium& medium, const Ray& r, float t_max, RNG& rng, Spectrum& beta,
                   MediumInteraction* mi);

}  // namespace skwr

#endif  // SKWR_KERNELS_SAMPLE_MEDIA_H_
