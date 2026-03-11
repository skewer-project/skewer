#ifndef SKWR_MEDIA_MEDIUMS_H_
#define SKWR_MEDIA_MEDIUMS_H_

#include <cstdint>

#include "core/math/vec3.h"
#include "core/spectral/spectrum.h"
#include "geometry/boundbox.h"

namespace skwr {

enum class MediumType : uint16_t { Vacuum = 0, Homogeneous = 1, Grid = 2, NanoVDB = 3 };

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

struct GridMedium {
    // Base properties (color/albedo of the smoke)
    Spectrum sigma_a_base;
    Spectrum sigma_s_base;
    float g;

    float max_density;

    BoundBox bbox;

    // Helper to get base extinction
    Spectrum Extinction() const { return sigma_a_base + sigma_s_base; }

    // // Procedural Soft Cloud: Density falls off linearly from the center
    // // Later, this will do a 3D array lookup or VDB sample.
    // float GetDensity(const Point3& p) const {
    //     Vec3 center = bbox.Centroid();
    //     float radius = (bbox.max().x() - bbox.min().x()) * 0.5f;
    //     float dist = (p - center).Length();
    //     if (dist >= radius) return 0.0f;

    //     // Smooth linear falloff: 1.0 at center, 0.0 at radius edge
    //     return 1.0f - (dist / radius);
    // }
    float GetDensity(const Point3& p) const {
        Vec3 center = bbox.Centroid();
        float radius = (bbox.max().x() - bbox.min().x()) * 0.5f;
        float dist = (p - center).Length();

        if (dist >= radius) return 0.0f;

        // 1. Base smooth falloff
        float base_density = 1.0f - (dist / radius);

        // 2. Cheap Procedural Noise (3D Sine Waves)
        // Multiply coordinates by a frequency factor to create "lumps"
        float freq = 6.0f;
        float noise = std::sin(p.x() * freq) * std::sin(p.y() * freq) * std::sin(p.z() * freq);

        // Map noise from [-1, 1] to [0, 1] and blend it into the cloud
        noise = (noise + 1.0f) * 0.5f;

        // 3. Combine and carve out empty space
        float final_density = base_density * noise;

        // Use a threshold to create hard wispy edges (like real smoke)
        return final_density > 0.15f ? final_density : 0.0f;
    }

    // TODO: When implementing OpenVDB + Cam update
    // Transform from World Space to Grid Local Space
    // --- STUBS FOR OPENVDB / VOXELS ---
    // openvdb::FloatGrid::Ptr density_grid;
    // Mat4 world_to_grid;
};

}  // namespace skwr

#endif  // SKWR_MEDIA_MEDIUMS_H_
