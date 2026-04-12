#ifndef SKWR_SCENE_ANIMATION_EVALUATOR_H_
#define SKWR_SCENE_ANIMATION_EVALUATOR_H_

#include <map>
#include <string>
#include <vector>

#include "core/math/matrix.h"
#include "io/scene_loader.h"

namespace skwr {

class AnimationEvaluator {
  public:
    static Matrix4 EvaluateNodeTransform(const std::string& node_id, float t,
                                         const std::map<std::string, SceneNode>& nodes);

  private:
    static Vec3 EvaluateTranslation(const AnimationChannelVec3& channel, float t);
    static Quaternion EvaluateRotation(const AnimationChannelQuat& channel, float t);
    static float EvaluateScale(const AnimationChannelFloat& channel, float t);

    template <typename T, typename K>
    static T Interpolate(const std::vector<K>& keyframes, float t,
                         const std::string& interpolation);
};

}  // namespace skwr

#endif  // SKWR_SCENE_ANIMATION_EVALUATOR_H_
