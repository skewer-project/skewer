#ifndef SKWR_MEDIA_MEDIUMS_H_
#define SKWR_MEDIA_MEDIUMS_H_

#include "core/spectral/spectrum.h"

namespace skwr {

// Explicit constants for our Bit-Packed IDs
constexpr uint16_t kMediumTypeVacuum = 0;
constexpr uint16_t kMediumTypeHomogeneous = 1;
constexpr uint16_t kMediumTypeGrid = 2;

constexpr uint16_t kMediumTypeShift = 14;
constexpr uint16_t kMediumIndexMask = 0x3FFF;  // 0011 1111 1111 1111

struct HomogeneousMedium {
    // Absorption and scattering coefficients (rgb or spectral)
    Spectrum sigma_a;
    Spectrum sigma_s;

    // Asymmetry parameter for the Henyey-Greenstein phase function (-1 to 1)
    float g;

    // Helper to get total extinction (sigma_t)
    Spectrum Extinction() const { return sigma_a + sigma_s; }
};

struct GridMedium {
    // Pointers or handles to your VDB/Dense 3D voxel grids
    const float* density_grid;

    Spectrum sigma_a_base;
    Spectrum sigma_s_base;
    float g;

    // The maximum possible density in the entire grid.
    // CRITICAL for Delta Tracking (the majorant).
    float max_density;

    // TODO: When implementing OpenVDB + Cam update
    // Transform from World Space to Grid Local Space
    // Mat4 world_to_grid;
};

}  // namespace skwr

#endif  // SKWR_MEDIA_MEDIUMS_H_
