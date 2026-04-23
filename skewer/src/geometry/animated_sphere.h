#ifndef SKWR_GEOMETRY_ANIMATED_SPHERE_H_
#define SKWR_GEOMETRY_ANIMATED_SPHERE_H_

#include <cmath>
#include <stdexcept>
#include <vector>

#include "geometry/sphere.h"
#include "scene/animation.h"
#include "scene/scene_graph.h"

namespace skwr {

struct AnimatedSphere {
    SphereData local_data{};
    std::vector<AnimatedTransform> transform_chain;
    int32_t emissive_light_index = -1;

    Sphere EvaluateAt(float t) const {
        if (local_data.center_is_world) {
            if (!transform_chain.empty()) {
                throw std::runtime_error(
                    "AnimatedSphere: world-space center requires empty transform chain");
            }
            return Sphere{local_data.center,
                          local_data.radius,
                          local_data.material_id,
                          emissive_light_index >= 0 ? emissive_light_index : local_data.light_index,
                          local_data.interior_medium,
                          local_data.exterior_medium,
                          local_data.priority};
        }
        TRS w = EvaluateTransformChain(transform_chain, t);
        if (!TRSIsUniformScale(w)) {
            throw std::runtime_error("Animated sphere requires uniform scale");
        }
        Vec3 c = TRSApplyPoint(w, local_data.center);
        float r = local_data.radius * std::fabs(w.scale.x());
        return Sphere{c,
                      r,
                      local_data.material_id,
                      emissive_light_index >= 0 ? emissive_light_index : local_data.light_index,
                      local_data.interior_medium,
                      local_data.exterior_medium,
                      local_data.priority};
    }
};

}  // namespace skwr

#endif
