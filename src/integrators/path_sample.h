#ifndef SKWR_INTEGRATORS_PATH_SAMPLE_H_
#define SKWR_INTEGRATORS_PATH_SAMPLE_H_

#include <vector>

#include "core/color/color.h"
#include "core/spectral/spectrum.h"

namespace skwr {

struct DeepSegment {
    float z_front;
    float z_back;
    RGB L;  // radiance; integrated over segment
    float alpha;
};

struct PathSample {
    Spectrum L;  // "Flat" beauty pass
    std::vector<DeepSegment> segments;

    // TODO: consider boost::small_vector or some optimization for stack vs heap alloc tradeoffs
    explicit PathSample(size_t reserveCount = 16) { segments.reserve(reserveCount); }
};

}  // namespace skwr

#endif  // SKWR_INTEGRATORS_PATH_SAMPLE_H_
