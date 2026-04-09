#include "scene/animation_evaluator.h"
#include "core/math/utils.h"
#include <algorithm>

namespace skwr {

Matrix4 AnimationEvaluator::EvaluateNodeTransform(const std::string& node_id, float t, 
                                                 const std::map<std::string, SceneNode>& nodes) {
    auto it = nodes.find(node_id);
    if (it == nodes.end()) return Matrix4::Identity();

    const SceneNode& node = it->second;

    Vec3 translation(0, 0, 0);
    if (node.channels.translation) {
        translation = EvaluateTranslation(*node.channels.translation, t);
    }

    Quaternion rotation = Quaternion::Identity();
    if (node.channels.rotation) {
        rotation = EvaluateRotation(*node.channels.rotation, t);
    }

    float scale = node.base_scale;
    if (node.channels.scale) {
        scale *= EvaluateScale(*node.channels.scale, t);
    }

    Matrix4 local = Matrix4::Translate(translation) * 
                   Matrix4::Rotate(rotation) * 
                   Matrix4::Scale(scale);

    if (!node.parent.empty()) {
        Matrix4 parent_transform = EvaluateNodeTransform(node.parent, t, nodes);
        return parent_transform * local;
    }

    return local;
}

Vec3 AnimationEvaluator::EvaluateTranslation(const AnimationChannelVec3& channel, float t) {
    if (channel.keyframes.empty()) return Vec3(0, 0, 0);
    if (channel.keyframes.size() == 1) return channel.keyframes[0].value;

    if (t <= channel.keyframes.front().t) return channel.keyframes.front().value;
    if (t >= channel.keyframes.back().t) return channel.keyframes.back().value;

    auto it = std::lower_bound(channel.keyframes.begin(), channel.keyframes.end(), t,
                              [](const KeyframeVec3& k, float t) { return k.t < t; });
    
    auto k1 = std::prev(it);
    auto k2 = it;

    float factor = (t - k1->t) / (k2->t - k1->t);
    
    if (channel.interpolation == "step") return k1->value;
    return Lerp(k1->value, k2->value, factor);
}

Quaternion AnimationEvaluator::EvaluateRotation(const AnimationChannelQuat& channel, float t) {
    if (channel.keyframes.empty()) return Quaternion::Identity();
    if (channel.keyframes.size() == 1) return channel.keyframes[0].value;

    if (t <= channel.keyframes.front().t) return channel.keyframes.front().value;
    if (t >= channel.keyframes.back().t) return channel.keyframes.back().value;

    auto it = std::lower_bound(channel.keyframes.begin(), channel.keyframes.end(), t,
                              [](const KeyframeQuat& k, float t) { return k.t < t; });
    
    auto k1 = std::prev(it);
    auto k2 = it;

    float factor = (t - k1->t) / (k2->t - k1->t);

    if (channel.interpolation == "step") return k1->value;
    return Slerp(k1->value, k2->value, factor);
}

float AnimationEvaluator::EvaluateScale(const AnimationChannelFloat& channel, float t) {
    if (channel.keyframes.empty()) return 1.0f;
    if (channel.keyframes.size() == 1) return channel.keyframes[0].value;

    if (t <= channel.keyframes.front().t) return channel.keyframes.front().value;
    if (t >= channel.keyframes.back().t) return channel.keyframes.back().value;

    auto it = std::lower_bound(channel.keyframes.begin(), channel.keyframes.end(), t,
                              [](const KeyframeFloat& k, float t) { return k.t < t; });
    
    auto k1 = std::prev(it);
    auto k2 = it;

    float factor = (t - k1->t) / (k2->t - k1->t);

    if (channel.interpolation == "step") return k1->value;
    return Lerp(k1->value, k2->value, factor);
}

} // namespace skwr
