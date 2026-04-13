#include "scene/animation.h"

#include <algorithm>
#include <cmath>

#include "core/math/quat.h"
#include "scene/interp_curve.h"

namespace skwr {

namespace {

inline Vec3 LerpVec3(const Vec3& a, const Vec3& b, float s) { return a + (b - a) * s; }

inline float EvalCurveOrLinear(const std::shared_ptr<const InterpolationCurve>& curve, float u) {
    if (curve) return curve->Evaluate(u);
    return BezierCurve::Linear().Evaluate(u);
}

}  // namespace

void AnimatedTransform::SortKeyframes() {
    std::sort(keyframes.begin(), keyframes.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

TRS AnimatedTransform::Evaluate(float t) const {
    if (keyframes.empty()) {
        return TRS{};
    }
    if (keyframes.size() == 1) {
        return keyframes[0].transform;
    }

    const Keyframe& first = keyframes.front();
    const Keyframe& last = keyframes.back();
    if (t <= first.time) {
        return first.transform;
    }
    if (t >= last.time) {
        return last.transform;
    }

    // keyframes[i].time <= t < keyframes[i+1].time
    auto it = std::upper_bound(keyframes.begin(), keyframes.end(), t,
                               [](float time, const Keyframe& k) { return time < k.time; });
    size_t i = static_cast<size_t>(std::distance(keyframes.begin(), it)) - 1;
    const Keyframe& k0 = keyframes[i];
    const Keyframe& k1 = keyframes[i + 1];
    float dt = k1.time - k0.time;
    if (dt <= 1e-20f) {
        return k1.transform;
    }
    float local_u = (t - k0.time) / dt;
    local_u = std::clamp(local_u, 0.0f, 1.0f);
    float alpha = EvalCurveOrLinear(k0.curve, local_u);

    TRS out{};
    out.translation = LerpVec3(k0.transform.translation, k1.transform.translation, alpha);
    out.scale = LerpVec3(k0.transform.scale, k1.transform.scale, alpha);
    out.rotation = QuatNormalize(QuatSlerp(k0.transform.rotation, k1.transform.rotation, alpha));
    return out;
}

}  // namespace skwr
