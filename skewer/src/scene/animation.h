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

inline TRS EvaluateTransformChain(const std::vector<AnimatedTransform>& chain, float t) {
    TRS acc{};
    for (const auto& at : chain) {
        acc = Compose(acc, at.Evaluate(t));
    }
    return acc;
}

inline bool TransformChainIsStatic(const std::vector<AnimatedTransform>& chain) {
    for (const auto& at : chain) {
        if (!at.IsStatic()) {
            return false;
        }
    }
    return true;
}

}  // namespace skwr

#endif
