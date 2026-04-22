#ifndef SKWR_IO_GRAPH_FROM_JSON_H_
#define SKWR_IO_GRAPH_FROM_JSON_H_

#include <memory>
#include <nlohmann/json_fwd.hpp>

#include "core/math/transform.h"
#include "scene/animation.h"

namespace skwr {

class InterpolationCurve;

// Parses translate / rotate (degrees) / scale (scalar or [x,y,z]) with defaults (identity).
TRS ParseTRSFields(const nlohmann::json& j);

std::shared_ptr<const InterpolationCurve> ParseCurveJson(const nlohmann::json& j);

// Full "transform" object: either { keyframes: [...] } or static TRS fields.
AnimatedTransform ParseAnimatedTransformJson(const nlohmann::json& j);

}  // namespace skwr

#endif
