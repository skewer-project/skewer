#ifndef SKWR_KERNELS_SAMPLE_RECORD_H_
#define SKWR_KERNELS_SAMPLE_RECORD_H_

#include "core/spectrum.h"
#include "core/vec3.h"

namespace skwr {

struct SampleRecord {
    Spectrum L;
    float transmittance;  // alpha
    float depth;
    Vec3 normal;
    bool hit_surface;
};

}  // namespace skwr

#endif  // SKWR_KERNELS_SAMPLE_RECORD_H_
