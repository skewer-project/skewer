#ifndef SKWR_GEOMETRY_TRIANGLE_H_
#define SKWR_GEOMETRY_TRIANGLE_H_

#include <cstdint>

#include "core/vec3.h"

namespace skwr {

struct Triangle {
    Vec3 p0;          // Vertex 0 position
    Vec3 e1;          // Edge 1: p1 - p0
    Vec3 e2;          // Edge 2: p2 - p0
    Vec3 n0, n1, n2;  // Per-vertex normals (all set to geometric normal for flat meshes)
    uint32_t material_id;
};

}  // namespace skwr

#endif  // SKWR_GEOMETRY_TRIANGLE_H_
