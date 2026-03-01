#ifndef SKWR_SCENE_MESH_UTILS_H_
#define SKWR_SCENE_MESH_UTILS_H_

#include "geometry/mesh.h"

// This is a temporary helper just to test out the ground plane in render sessions

namespace skwr {

// Creates a flat quad mesh (2 triangles)
// p0 (bottom-left), p1 (bottom-right), p2 (top-right), p3 (top-left)
inline Mesh CreateQuad(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                       uint32_t mat_id) {
    Mesh mesh;
    mesh.material_id = mat_id;

    // 1. Vertices (CCW winding)
    mesh.p = {p0, p1, p2, p3};

    // 2. Indices (Two triangles sharing the diagonal)
    // Tri 1: 0 -> 1 -> 2
    // Tri 2: 0 -> 2 -> 3
    mesh.indices = {0, 1, 2, 0, 2, 3};

    // 3. Normals (Optional but good for debugging)
    // All point up relative to the quad surface
    Vec3 n = Normalize(Cross(p1 - p0, p3 - p0));
    mesh.n = {n, n, n, n};

    // 4. UVs (Standard 0-1 mapping)
    mesh.uv = {Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f),
               Vec3(0.0f, 1.0f, 0.0f)};

    return mesh;
}

}  // namespace skwr

#endif
