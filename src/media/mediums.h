#ifndef SKWR_MEDIA_MEDIUMS_H_
#define SKWR_MEDIA_MEDIUMS_H_

#include <cstdint>

#include "core/spectral/spectrum.h"

namespace skwr {

enum class MediumType : uint16_t { Vacuum = 0, Homogeneous = 1, Grid = 2 };

// Bit-packing layout constants
constexpr uint16_t kMediumTypeShift = 14;
constexpr uint16_t kMediumIndexMask = 0x3FFF;  // 0011 1111 1111 1111

// Helper functions to cleanly encapsulate the casting verbosity
inline uint16_t PackMediumId(MediumType type, uint16_t index) {
    // Cast the enum to an integer to shift it, then bitwise-OR with the index
    return (static_cast<uint16_t>(type) << kMediumTypeShift) | (index & kMediumIndexMask);
}

inline MediumType ExtractMediumType(uint16_t packed_id) {
    return static_cast<MediumType>(packed_id >> kMediumTypeShift);
}

inline uint16_t ExtractMediumIndex(uint16_t packed_id) { return packed_id & kMediumIndexMask; }

struct HomogeneousMedium {
    // Absorption and scattering coefficients (rgb or spectral)
    Spectrum sigma_a;
    Spectrum sigma_s;

    // Asymmetry parameter for the Henyey-Greenstein phase function (-1 to 1)
    float g;

    // Helper to get total extinction (sigma_t)
    Spectrum Extinction() const { return sigma_a + sigma_s; }
};

/* TODO */
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
