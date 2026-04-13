#ifndef SKWR_SCENE_SCENE_GRAPH_H_
#define SKWR_SCENE_SCENE_GRAPH_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/math/vec3.h"
#include "scene/animation.h"

namespace skwr {

enum class NodeType : uint8_t { Group, Mesh, Sphere };

struct SphereData {
    Vec3 center;
    float radius = 1.0f;
    uint32_t material_id = 0;
    int32_t light_index = -1;
    uint16_t interior_medium = 0;
    uint16_t exterior_medium = 0;
    uint16_t priority = 1;
    // When true (NanoVDB-derived bounds), center/radius are world-space; parent_world must be
    // identity.
    bool center_is_world = false;
};

struct SceneNode {
    std::string name;
    NodeType type = NodeType::Group;
    AnimatedTransform anim_transform;
    std::vector<SceneNode> children;
    std::vector<uint32_t> mesh_ids;
    std::optional<SphereData> sphere_data;
};

}  // namespace skwr

#endif
