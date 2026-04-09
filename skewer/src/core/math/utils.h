#ifndef SKWR_CORE_MATH_UTILS_H_
#define SKWR_CORE_MATH_UTILS_H_

#include "core/math/constants.h"
#include "core/math/vec3.h"

namespace skwr {

inline float DegreesToRadians(float degrees) { return degrees * kPi / 180.0f; }

inline float Lerp(float a, float b, float t) { return a + t * (b - a); }

inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + t * (b - a); }

}  // namespace skwr

#endif  // SKWR_CORE_MATH_UTILS_H_

