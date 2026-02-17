#ifndef SKWR_CORE_CONSTANTS_H_
#define SKWR_CORE_CONSTANTS_H_

#include <cstdint>
#include <limits>

namespace skwr {

using Float = float;  // Global precision switch (can change to double)
// typedef float Float;  // Global precision switch (can change to double)

constexpr float kInfinity = std::numeric_limits<Float>::infinity();
constexpr float kPi = 3.1415926535897932385F;
constexpr float kTau = 2.0F * kPi;
static constexpr float kStraightAngle = 180.0F;
constexpr float kParallelThreshold = 0.9F;

constexpr uint64_t kRNGStateSeed = 0x853c49e6748fea9bULL;
constexpr uint64_t kRNGIncSeed = 0xda3e39cb94b95bdbULL;

constexpr uint64_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;

constexpr float kMinVal = 1e-8F;

// std::numeric_limits::epsilon() is the gap between 1.0 and the next value.
// Subtract half of it to be safe, or the whole thing.
constexpr float kOneMinusEpsilon = 0x1.fffffep-1;
// OR simpler C++ style:
// static constexpr Float OneMinusEpsilon = 1.0f - std::numeric_limits<Float>::epsilon();

constexpr Float kShadowEpsilon = 0.001F;
constexpr float kBoundEpsilon = 0.0001F;

inline auto DegreesToRadians(Float degrees) -> float { return degrees * kPi / kStraightAngle; }

}  // namespace skwr

#endif  // SKWR_CORE_CONSTANTS_H_
