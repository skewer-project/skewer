#ifndef SKWR_CORE_MATH_UTILS_H_
#define SKWR_CORE_MATH_UTILS_H_

#include "core/math/constants.h"

namespace skwr {

inline float DegreesToRadians(float degrees) { return degrees * MathConstants::kPi / 180.0f; }

}  // namespace skwr

#endif  // SKWR_CORE_MATH_UTILS_H_
