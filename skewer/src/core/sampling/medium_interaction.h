#ifndef SKWR_CORE_SAMPLING_MEDIUM_INTERACTION_H_
#define SKWR_CORE_SAMPLING_MEDIUM_INTERACTION_H_

#include "core/math/vec3.h"
#include "core/spectral/spectrum.h"

namespace skwr {

struct MediumInteraction {
    Point3 point;
    Vec3 wo;      // Outgoing direction (points back towards the ray origin, -ray.dir).
    float t;      // Distance along the ray where the scatter occurred.
    float alpha;  // For volumetric deep output

    /** Phase Function Data
     * Instead of a polymorphic pointer, we store the anisotropy parameter 'g'.
     * g = 0   (Isotropic, scatters equally everywhere)
     * g > 0   (Forward scattering, like fog or clouds)
     * g < 0   (Backward scattering, rare but possible)
     */
    float phase_g;

    // For homogeneous media, this is constant. For grid media, this is the
    // interpolated value from the voxel grid at point 'p'.
    Spectrum sigma_s;  // scattering coefficient at point (for NEE/MIS)
};

}  // namespace skwr

#endif  // SKWR_CORE_SAMPLING_MEDIUM_INTERACTION_H_
