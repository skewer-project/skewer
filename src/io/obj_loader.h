#ifndef SKWR_IO_OBJ_LOADER_H_
#define SKWR_IO_OBJ_LOADER_H_

// OBJ file loader for v2 renderer.
// Loads .obj files via tinyobjloader, converts materials to v2 Material structs,
// and populates a Scene with Mesh geometry.

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/constants.h"
#include "core/vec3.h"
#include "geometry/mesh.h"
#include "materials/material.h"
#include "scene/scene.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

namespace skwr {

// Convert a tinyobj material to a v2 Material struct.
// Mapping strategy (mirrors v1 priority order):
//   1. PBR metallic >= 0.5        -> Metal
//   2. Dissolve < 0.99 or glass   -> Dielectric
//   3. High specular (non-PBR)    -> Metal
//   4. Default                    -> Lambertian
inline Material ConvertObjMaterial(const tinyobj::material_t& mtl) {
    Material mat{};

    std::clog << "  Material: \"" << mtl.name << "\""
              << " Kd=(" << mtl.diffuse[0] << ", " << mtl.diffuse[1] << ", " << mtl.diffuse[2]
              << ") Pm=" << mtl.metallic << " Pr=" << mtl.roughness << " d=" << mtl.dissolve
              << " Ni=" << mtl.ior << std::endl;

    // 1. PBR METALLIC
    if (mtl.metallic >= 0.5f) {
        mat.type = MaterialType::Metal;
        mat.albedo = Spectrum(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);
        mat.roughness = std::max(0.0f, std::min(1.0f, mtl.roughness * 0.5f));
        std::clog << "    -> Metal (PBR)" << std::endl;
        return mat;
    }

    // 2. TRANSPARENCY / GLASS
    bool is_glass_illum = (mtl.illum == 4 || mtl.illum == 6 || mtl.illum == 7 || mtl.illum == 9);
    if (mtl.dissolve < 0.99f || is_glass_illum) {
        mat.type = MaterialType::Dielectric;
        mat.albedo = Spectrum(1.0f, 1.0f, 1.0f);
        mat.roughness = 0.0f;
        mat.ior = (mtl.ior > 1.0f) ? mtl.ior : 1.5f;
        std::clog << "    -> Dielectric (ior=" << mat.ior << ")" << std::endl;
        return mat;
    }

    // 3. TRADITIONAL SPECULAR (non-PBR fallback)
    float spec_intensity = (mtl.specular[0] + mtl.specular[1] + mtl.specular[2]) / 3.0f;
    if (spec_intensity > 0.5f && mtl.metallic < 0.001f) {
        mat.type = MaterialType::Metal;
        mat.albedo = Spectrum(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);
        float fuzz = 1.0f - std::min(1.0f, mtl.shininess / 1000.0f);
        mat.roughness = std::max(0.0f, std::min(0.5f, fuzz));
        std::clog << "    -> Metal (specular)" << std::endl;
        return mat;
    }

    // 4. DEFAULT - Lambertian diffuse
    mat.type = MaterialType::Lambertian;
    mat.albedo = Spectrum(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);
    mat.roughness = 1.0f;

    // If diffuse is near-zero, use a default gray
    if (mtl.diffuse[0] + mtl.diffuse[1] + mtl.diffuse[2] < 0.001f) {
        mat.albedo = Spectrum(0.5f, 0.5f, 0.5f);
        std::clog << "    -> Lambertian (default gray)" << std::endl;
    } else {
        std::clog << "    -> Lambertian" << std::endl;
    }

    return mat;
}

// Load an OBJ file and populate the Scene with meshes and materials.
// scale: per-axis scale applied to vertex positions (e.g. Vec3(1,1,1) = no scaling).
// Returns true on success.
inline bool LoadOBJ(const std::string& filename, Scene& scene,
                    const Vec3& scale = Vec3(1.0f, 1.0f, 1.0f)) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // Extract base path for material/texture loading
    std::string base_path;
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        base_path = filename.substr(0, last_slash);
    }

    std::clog << "[OBJ] Loading: " << filename << std::endl;

    bool success =
        tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(),
                         base_path.empty() ? nullptr : base_path.c_str(), true /* triangulate */);

    if (!warn.empty()) std::cerr << "[OBJ] Warning: " << warn << std::endl;
    if (!err.empty()) std::cerr << "[OBJ] Error: " << err << std::endl;
    if (!success) {
        std::cerr << "[OBJ] Failed to load: " << filename << std::endl;
        return false;
    }

    // Auto-fit: compute bounding box and normalize so the object fits in a 2-unit cube
    // (matching the diameter of the sphere it replaces). User scale is applied on top.
    Vec3 bbox_min(kInfinity, kInfinity, kInfinity);
    Vec3 bbox_max(-kInfinity, -kInfinity, -kInfinity);
    for (size_t i = 0; i < attrib.vertices.size(); i += 3) {
        for (int a = 0; a < 3; a++) {
            if (attrib.vertices[i + a] < bbox_min[a]) bbox_min[a] = attrib.vertices[i + a];
            if (attrib.vertices[i + a] > bbox_max[a]) bbox_max[a] = attrib.vertices[i + a];
        }
    }
    Vec3 extent = bbox_max - bbox_min;
    Vec3 bbox_center = (bbox_min + bbox_max) * 0.5f;
    Float max_extent = std::max({extent.x(), extent.y(), extent.z()});
    Float normalize = (max_extent > 0.0f) ? (2.0f / max_extent) : 1.0f;
    Vec3 final_scale(scale.x() * normalize, scale.y() * normalize, scale.z() * normalize);

    std::clog << "[OBJ] Bounding box: (" << bbox_min << ") - (" << bbox_max << ")" << std::endl;
    std::clog << "[OBJ] Center: (" << bbox_center << ")" << std::endl;
    std::clog << "[OBJ] Auto-fit scale: " << normalize
              << ", final scale: (" << final_scale << ")" << std::endl;

    // Convert OBJ materials -> v2 Material IDs
    // material_id_map[obj_mat_index] = scene material ID
    std::vector<uint32_t> material_id_map;
    material_id_map.reserve(materials.size());

    std::clog << "[OBJ] Converting " << materials.size() << " materials" << std::endl;
    for (const auto& mtl : materials) {
        Material converted = ConvertObjMaterial(mtl);
        material_id_map.push_back(scene.AddMaterial(converted));
    }

    // Fallback material for faces with no material assignment
    uint32_t fallback_mat_id = UINT32_MAX;

    auto GetOrCreateFallback = [&]() -> uint32_t {
        if (fallback_mat_id == UINT32_MAX) {
            Material fallback{};
            fallback.type = MaterialType::Lambertian;
            fallback.albedo = Spectrum(0.5f, 0.5f, 0.5f);
            fallback.roughness = 1.0f;
            fallback_mat_id = scene.AddMaterial(fallback);
        }
        return fallback_mat_id;
    };

    // Process each shape.
    // Since v2 Mesh has a single material_id, we group faces by material
    // within each shape and create one Mesh per (shape, material) group.
    uint32_t total_triangles = 0;

    for (const auto& shape : shapes) {
        // Group face indices by material ID
        // Key: OBJ material index (-1 for no material)
        std::unordered_map<int, std::vector<size_t>> mat_to_faces;

        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int mat_id = shape.mesh.material_ids[f];
            mat_to_faces[mat_id].push_back(f);
        }

        // Create one Mesh per material group
        for (auto& [obj_mat_id, face_indices] : mat_to_faces) {
            Mesh mesh;

            // Resolve material
            if (obj_mat_id >= 0 && obj_mat_id < static_cast<int>(material_id_map.size())) {
                mesh.material_id = material_id_map[obj_mat_id];
            } else {
                mesh.material_id = GetOrCreateFallback();
            }

            // Vertex deduplication map: original OBJ vertex index -> local mesh index
            // Key is (vertex_index, normal_index) pair encoded as uint64
            std::unordered_map<uint64_t, uint32_t> vertex_map;
            bool has_normals = !attrib.normals.empty();

            for (size_t f : face_indices) {
                size_t fv = shape.mesh.num_face_vertices[f];
                if (fv != 3)
                    continue;  // Skip non-triangles (shouldn't happen with triangulate=true)

                // Compute the face's index offset into the shape's index buffer
                size_t index_offset = 0;
                for (size_t k = 0; k < f; k++) {
                    index_offset += shape.mesh.num_face_vertices[k];
                }

                uint32_t tri_indices[3];

                for (int v = 0; v < 3; v++) {
                    tinyobj::index_t idx = shape.mesh.indices[index_offset + v];

                    // Encode vertex+normal index pair for deduplication
                    uint64_t key = (static_cast<uint64_t>(idx.vertex_index) << 32);
                    if (has_normals && idx.normal_index >= 0) {
                        key |= static_cast<uint64_t>(static_cast<uint32_t>(idx.normal_index));
                    }

                    auto it = vertex_map.find(key);
                    if (it != vertex_map.end()) {
                        tri_indices[v] = it->second;
                    } else {
                        uint32_t local_idx = static_cast<uint32_t>(mesh.p.size());
                        vertex_map[key] = local_idx;
                        tri_indices[v] = local_idx;

                        // Position: center at origin, then apply auto-fit + user scale
                        mesh.p.push_back(Vec3(
                            (attrib.vertices[3 * idx.vertex_index + 0] - bbox_center.x()) * final_scale.x(),
                            (attrib.vertices[3 * idx.vertex_index + 1] - bbox_center.y()) * final_scale.y(),
                            (attrib.vertices[3 * idx.vertex_index + 2] - bbox_center.z()) * final_scale.z()));

                        // Normal (if available)
                        if (has_normals && idx.normal_index >= 0) {
                            mesh.n.push_back(Vec3(attrib.normals[3 * idx.normal_index + 0],
                                                  attrib.normals[3 * idx.normal_index + 1],
                                                  attrib.normals[3 * idx.normal_index + 2]));
                        }
                    }
                }

                mesh.indices.push_back(tri_indices[0]);
                mesh.indices.push_back(tri_indices[1]);
                mesh.indices.push_back(tri_indices[2]);
            }

            // If normals were partially available, clear them to avoid mismatched sizes
            if (!mesh.n.empty() && mesh.n.size() != mesh.p.size()) {
                mesh.n.clear();
            }

            total_triangles += static_cast<uint32_t>(mesh.indices.size() / 3);
            scene.AddMesh(std::move(mesh));
        }
    }

    std::clog << "[OBJ] Loaded " << shapes.size() << " shapes, " << total_triangles
              << " triangles, " << materials.size() << " materials" << std::endl;

    return true;
}

}  // namespace skwr

#endif  // SKWR_IO_OBJ_LOADER_H_
