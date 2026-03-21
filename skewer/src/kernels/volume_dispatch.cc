#include "kernels/volume_dispatch.h"

#include <cstdint>

#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectrum.h"
#include "core/transport/medium_interaction.h"
#include "kernels/sample_media.h"
#include "media/mediums.h"
#include "scene/scene.h"

namespace skwr {

/* Volume Dispatcher - returns true if scattering event occurs, false if hit surface */
auto SampleMedium(const Ray& ray, const Scene& scene, float t_max, RNG& rng, Spectrum& beta,
                  MediumInteraction* mi, const SampledWavelengths& wl) -> bool {
    uint16_t active_id = ray.vol_stack().GetActiveMedium();

    if (active_id == 0 || active_id == kVacuumMediumId) { return false;
}

    // Decode the Bit-Packed ID
    uint16_t const type = active_id >> kMediumTypeShift;
    uint16_t index = active_id & kMediumIndexMask;

    switch (type) {
        case static_cast<int>(MediumType::Vacuum):
            // No attenuation, ray passes straight to the surface.
            return false;

        case static_cast<int>(MediumType::Homogeneous):
            if (index >= scene.homogeneous_media().size()) { return false;
}
            return SampleHomogeneous(scene.homogeneous_media()[index], ray, t_max, rng, beta, mi);

        case static_cast<int>(MediumType::Grid):
            if (index >= scene.grid_media().size()) { return false;
}
            return SampleGrid(scene.grid_media()[index], ray, t_max, rng, beta, mi);

        case static_cast<int>(MediumType::NanoVDB):
            if (index >= scene.nanovdb_media().size()) { return false;
}
            return SampleNanoVDB(scene.nanovdb_media()[index], ray, t_max, rng, beta, mi, wl);

        default:
            return false;  // Fallback
    }
}

}  // namespace skwr
