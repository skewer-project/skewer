#ifndef SKWR_CORE_MATH_CONSTANTS_H_
#define SKWR_CORE_MATH_CONSTANTS_H_

#include <cstddef>
#include <cstdint>
#include <limits>

namespace skwr {

namespace MathConstants {
constexpr float kFloatInfinity = std::numeric_limits<float>::max();
constexpr float kPi = 3.1415926535897932385f;
constexpr float kInvPi = 0.31830988618379067154f;
constexpr uint64_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;

// std::numeric_limits::epsilon() is the gap between 1.0 and the next value.
// Subtract half of it to be safe, or the whole thing.
static constexpr float kOneMinusEpsilon = 0x1.fffffep-1;
// OR simpler C++ style:
// static constexpr float OneMinusEpsilon = 1.0f - std::numeric_limits<float>::epsilon();
}  // namespace MathConstants

namespace Numeric {
constexpr float kFloatEpsilon = std::numeric_limits<float>::epsilon();
constexpr float kNearZeroEpsilon = 1e-8f;
}  // namespace Numeric

namespace RenderConstants {
constexpr float kRayOffsetEpsilon = 1e-3f;
constexpr float kBoundsEpsilon = 1e-4f;
constexpr float kFarClip = 1e10f;
constexpr float kIsotropicPhaseEpsilon = 1e-3f;
}  // namespace RenderConstants

namespace Rec709 {
constexpr float kWeightRed = 0.2126f;
constexpr float kWeightRedSquared = kWeightRed * kWeightRed;

constexpr float kWeightGreen = 0.7152f;
constexpr float kWeightGreenSquared = kWeightGreen * kWeightGreen;

constexpr float kWeightBlue = 0.0722f;
constexpr float kWeightBlueSquared = kWeightBlue * kWeightBlue;
}  // namespace Rec709

namespace Bezier {
constexpr float kBezierNewtonEps = 1e-6f;
constexpr int kBezierNewtonMaxIter = 32;
}  // namespace Bezier

namespace Memory {
// Maximum number of merged depth buckets stored per pixel. Sized to handle
// realistic scene depth complexity (a handful of overlapping surfaces and
// volumes per pixel) while keeping per-pixel deep storage bounded at compile
// time. Forced eviction kicks in when this is exceeded.
constexpr std::size_t kMaxDeepBuckets = 16;

// How many buckets each pixel reserves inline before spilling to the heap.
// Production stats show the average pixel holds ~1 bucket, with a long tail
// approaching kMaxDeepBuckets. Inline cap is sized so the common case never
// allocates; saturated pixels pay a few small heap reallocs apiece.
constexpr std::size_t kInlineDeepBuckets = 4;
}  // namespace Memory

}  // namespace skwr

#endif  // SKWR_CORE_MATH_CONSTANTS_H_
