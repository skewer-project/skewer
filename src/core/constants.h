#ifndef SKWR_CORE_CONSTANTS_H_
#define SKWR_CORE_CONSTANTS_H_

#include <cstdint>
#include <limits>

namespace skwr {

constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr float kPi = 3.1415926535897932385f;
constexpr float kInvPi = 0.31830988618379067154f;
constexpr uint64_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;

// std::numeric_limits::epsilon() is the gap between 1.0 and the next value.
// Subtract half of it to be safe, or the whole thing.
static constexpr float kOneMinusEpsilon = 0x1.fffffep-1;
// OR simpler C++ style:
// static constexpr float OneMinusEpsilon = 1.0f - std::numeric_limits<float>::epsilon();

constexpr float kShadowEpsilon = 0.001f;
constexpr float kBoundEpsilon = 0.0001f;
constexpr float kFarClip = 1e10f;

inline float DegreesToRadians(float degrees) { return degrees * kPi / 180.0f; }

}  // namespace skwr

#endif  // SKWR_CORE_CONSTANTS_H_
