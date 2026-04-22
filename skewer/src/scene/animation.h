#ifndef SKWR_SCENE_ANIMATION_H_
#define SKWR_SCENE_ANIMATION_H_

#include <memory>
#include <vector>

#include "core/math/transform.h"

namespace skwr {

class InterpolationCurve;

struct Keyframe {
    float time = 0.0f;
    TRS transform;
    // Easing from this keyframe toward the next; ignored on last keyframe.
    std::shared_ptr<const InterpolationCurve> curve;
};

struct AnimatedTransform {
    std::vector<Keyframe> keyframes;

    TRS Evaluate(float t) const;

    bool IsStatic() const { return keyframes.size() <= 1; }

    void SortKeyframes();
};

}  // namespace skwr

#endif
