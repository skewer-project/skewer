#ifndef SKWR_CORE_MATH_CONSTANTS_H_
#define SKWR_CORE_MATH_CONSTANTS_H_

#include <cstdint>
#include <limits>

namespace skwr {

constexpr float kInfinity = std::numeric_limits<float>::max();
constexpr float kPi = 3.1415926535897932385f;
constexpr float kInvPi = 0.31830988618379067154f;
constexpr uint64_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;

// std::numeric_limits::epsilon() is the gap between 1.0 and the next value.
// Subtract half of it to be safe, or the whole thing.
static constexpr float kOneMinusEpsilon = 0x1.fffffep-1;
// OR simpler C++ style:
// static constexpr float OneMinusEpsilon = 1.0f - std::numeric_limits<float>::epsilon();

constexpr float kEpsilon = std::numeric_limits<float>::epsilon();
constexpr float kShadowEpsilon = 0.001f;
constexpr float kBoundEpsilon = 0.0001f;
constexpr float kZeroEpsilon = 1e-8f;
constexpr float kFarClip = 1e10f;

inline float DegreesToRadians(float degrees) { return degrees * kPi / 180.0f; }

namespace Rec709 {
constexpr float kWeightRed = 0.2126f;
constexpr float kWeightRedSquared = kWeightRed * kWeightRed;

constexpr float kWeightGreen = 0.7152f;
constexpr float kWeightGreenSquared = kWeightGreen * kWeightGreen;

constexpr float kWeightBlue = 0.0722f;
constexpr float kWeightBlueSquared = kWeightBlue * kWeightBlue;
}  // namespace Rec709

}  // namespace skwr

#endif  // SKWR_CORE_MATH_CONSTANTS_H_
