#ifndef SKWR_KERNELS_VOLUME_DISPATCH_H_
#define SKWR_KERNELS_VOLUME_DISPATCH_H_

#include <cstdint>

#include "core/ray.h"
#include "core/sampling/medium_interaction.h"
#include "core/sampling/rng.h"
#include "kernels/sample_homogeneous.h"
#include "media/mediums.h"
#include "scene/scene.h"

namespace skwr {
/* Volume Dispatcher - returns true if scattering event occurs, false if hit surface */
inline bool SampleMedium(const Ray& ray, const Scene& scene, float t_max, RNG& rng, Spectrum& beta,
                         MediumInteraction& mi) {
    uint16_t active_id = ray.vol_stack().GetActiveMedium();

    // Decode the Bit-Packed ID
    uint16_t type = active_id >> kMediumTypeShift;
    uint16_t index = active_id & kMediumIndexMask;

    switch (type) {
        case static_cast<int>(MediumType::Vacuum):
            // No attenuation, ray passes straight to the surface.
            return false;

        case static_cast<int>(MediumType::Homogeneous):
            return SampleHomogeneous(scene.homogeneous_media()[index], ray, t_max, rng, beta, mi);

        case static_cast<int>(MediumType::Grid):
            //     return SampleGrid(grid_media[index], ray, t_max, rng, beta, mi);
            return false;

        default:
            return false;  // Fallback
    }
}

}  // namespace skwr

#endif  // SKWR_KERNELS_VOLUME_DISPATCH_H_
