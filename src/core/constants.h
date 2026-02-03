#ifndef SKWR_CORE_CONSTANTS_H_
#define SKWR_CORE_CONSTANTS_H_

#include <limits>

namespace skwr {

using Float = float;  // Global precision switch (can change to double)

constexpr Float kInfinity = std::numeric_limits<Float>::infinity();
constexpr Float kPi = 3.1415926535897932385f;
// std::numeric_limits::epsilon() is the gap between 1.0 and the next value.
// Subtract half of it to be safe, or the whole thing.
static constexpr Float OneMinusEpsilon = 0x1.fffffep-1;
// OR simpler C++ style:
// static constexpr Float OneMinusEpsilon = 1.0f - std::numeric_limits<Float>::epsilon();

inline Float DegreesToRadians(Float degrees) { return degrees * kPi / 180.0f; }

}  // namespace skwr

#endif  // SKWR_CORE_CONSTANTS_H_
