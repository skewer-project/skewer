#ifndef SKWR_GEOMETRY_MESH_H_
#define SKWR_GEOMETRY_MESH_H_

#include <cstdint>
#include <vector>

#include "core/vec3.h"

namespace skwr {

struct Mesh {
    // Structure of Arrays style data
    std::vector<Vec3> p;   // Positions
    std::vector<Vec3> n;   // Normals (optional, could be empty)
    std::vector<Vec3> uv;  // Texture coords (z unused, access via .x()/.y())

    // Index buffer
    std::vector<uint32_t> indices;
    uint32_t material_id;
};

}  // namespace skwr

#endif  // SKWR_GEOMETRY_MESH_H_
