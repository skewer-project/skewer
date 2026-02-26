#ifndef SKWR_INTEGRATORS_PATH_SAMPLE_H_
#define SKWR_INTEGRATORS_PATH_SAMPLE_H_

#include <vector>

#include "core/color.h"
#include "core/spectrum.h"

namespace skwr {

struct DeepSegment {
    float z_front;
    float z_back;
    RGB L;  // radiance; integrated over segment
    float alpha;
};

struct PathSample {
    Spectrum L;      // "Flat" beauty pass (premultiplied radiance)
    float alpha;     // Coverage for the flat beauty pass (0 = transparent, 1 = opaque)
    std::vector<DeepSegment> segments;

    // TODO: consider boost::small_vector or some optimization for stack vs heap alloc tradeoffs
    explicit PathSample(size_t reserveCount = 16) : alpha(0.0f) {
        segments.reserve(reserveCount);
    }
};

}  // namespace skwr

#endif  // SKWR_INTEGRATORS_PATH_SAMPLE_H_
