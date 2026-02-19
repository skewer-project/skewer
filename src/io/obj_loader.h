#ifndef SKWR_IO_OBJ_LOADER_H_
#define SKWR_IO_OBJ_LOADER_H_

// OBJ file loader for v2 renderer.
// Loads .obj files via tinyobjloader, converts materials to v2 Material structs,
// and populates a Scene with Mesh geometry.

#include <string>

#include "core/vec3.h"
#include "materials/material.h"
#include "scene/scene.h"

namespace tinyobj {
struct material_t;
}  // namespace tinyobj

namespace skwr {

// Convert a tinyobj material to a v2 Material struct.
// Mapping strategy (mirrors v1 priority order):
//   1. PBR metallic >= 0.5        -> Metal
//   2. Dissolve < 0.99 or glass   -> Dielectric
//   3. High specular (non-PBR)    -> Metal
//   4. Default                    -> Lambertian
Material ConvertObjMaterial(const tinyobj::material_t& mtl);

// Load an OBJ file and populate the Scene with meshes and materials.
// scale: per-axis scale applied to vertex positions (e.g. Vec3(1,1,1) = no scaling).
// auto_fit: when true, normalizes the model to a 2-unit cube centered at origin before applying
// scale. Returns true on success.
bool LoadOBJ(const std::string& filename, Scene& scene, const Vec3& scale = Vec3(1.0f, 1.0f, 1.0f),
             bool auto_fit = true);

}  // namespace skwr

#endif  // SKWR_IO_OBJ_LOADER_H_
