#ifndef SKWR_CORE_TRANSPORT_DEEP_SEGMENT_H_
#define SKWR_CORE_TRANSPORT_DEEP_SEGMENT_H_

#include "core/color/color.h"

namespace skwr {

struct DeepSegment {
    float z_front;
    float z_back;
    RGB L;  // radiance; integrated over segment
    float alpha;
};

}  // namespace skwr

#endif  // SKWR_CORE_TRANSPORT_DEEP_SEGMENT_H_
