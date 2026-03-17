#ifndef SKWR_CORE_TRANSPORT_PATH_SAMPLE_H_
#define SKWR_CORE_TRANSPORT_PATH_SAMPLE_H_

#include "core/containers/bounded_array.h"
#include "core/cpu_config.h"
#include "core/spectral/spectrum.h"
#include "core/transport/deep_segment.h"

namespace skwr {

struct PathSample {
    Spectrum L;          // "Flat" beauty pass (premultiplied radiance)
    float alpha = 0.0f;  // Coverage for the flat beauty pass (0 = transparent, 1 = opaque)
    BoundedArray<DeepSegment, kMaxDeepSegments> segments;
};

}  // namespace skwr

#endif  // SKWR_CORE_TRANSPORT_PATH_SAMPLE_H_
