#ifndef SKWR_INTEGRATORS_PATH_SAMPLE_H_
#define SKWR_INTEGRATORS_PATH_SAMPLE_H_

#include <vector>

#include "core/spectrum.h"

namespace skwr {

struct DeepSegment {
    float z_front;
    float z_back;
    Spectrum L;  // radiance; integrated over segment
    float alpha;
};

struct PathSample {
    Spectrum L;  // "Flat" beauty pass
    std::vector<DeepSegment> segments;
};

}  // namespace skwr

#endif  // SKWR_INTEGRATORS_PATH_SAMPLE_H_
