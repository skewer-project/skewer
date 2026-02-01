#ifndef SKWR_CORE_CONSTANTS_H_
#define SKWR_CORE_CONSTANTS_H_

#include <limits>

namespace skwr {

using Float = float;  // Global precision switch (can change to double)

constexpr Float kInfinity = std::numeric_limits<Float>::infinity();
constexpr Float kPi = 3.1415926535897932385f;

inline Float DegreesToRadians(Float degrees) { return degrees * kPi / 180.0f; }

}  // namespace skwr

#endif  // SKWR_CORE_CONSTANTS_H_
