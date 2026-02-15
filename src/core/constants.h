#ifndef SKWR_CORE_CONSTANTS_H_
#define SKWR_CORE_CONSTANTS_H_

#include <limits>

namespace skwr {

constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr float kPi = 3.1415926535897932385f;

// std::numeric_limits::epsilon() is the gap between 1.0 and the next value.
// Subtract half of it to be safe, or the whole thing.
static constexpr float kOneMinusEpsilon = 0x1.fffffep-1;
// OR simpler C++ style:
// static constexpr float OneMinusEpsilon = 1.0f - std::numeric_limits<float>::epsilon();

constexpr float kShadowEpsilon = 0.001f;

inline float DegreesToRadians(float degrees) { return degrees * kPi / 180.0f; }

}  // namespace skwr

#endif  // SKWR_CORE_CONSTANTS_H_
