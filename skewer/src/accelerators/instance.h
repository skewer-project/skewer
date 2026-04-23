#ifndef SKWR_ACCELERATORS_INSTANCE_H_
#define SKWR_ACCELERATORS_INSTANCE_H_

#include <cstdint>
#include <vector>

#include "geometry/boundbox.h"
#include "scene/animation.h"

namespace skwr {

// A scene instance: a BLAS reference plus the animated transform chain that places it in the world.
// Static instances cache a baked world-from-local TRS; animated ones evaluate the chain at
// ray.time() during intersection.
struct Instance {
    uint32_t blas_id = 0;
    std::vector<AnimatedTransform> transform_chain;
    bool is_static = true;
    TRS static_world_from_local{};
    BoundBox world_bounds;
    uint32_t first_light_index = 0;
    uint32_t light_count = 0;
    std::vector<int32_t> tri_light_indices;
};

}  // namespace skwr

#endif
