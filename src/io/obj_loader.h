#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H
//==============================================================================================
// OBJ file loader for ray tracing. Loads .obj files with .mtl material support.
// Converts OBJ materials to raytracer materials and builds a BVH for acceleration.
//==============================================================================================

#include <string>
#include <vector>

#include "geometry/bvh.h"
#include "geometry/hittable.h"
#include "geometry/triangle.h"
#include "materials/material.h"
#include "materials/texture.h"
#include "renderer/scene.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

// Convert a tinyobj material to a raytracer material
// Uses PBR-first approach to properly handle Blender's Principled BSDF exports
inline shared_ptr<material> convert_obj_material(const tinyobj::material_t& mtl,
                                                 const std::string& base_path) {
    // Helper to format color for debug output
    auto fmt_color = [](float r, float g, float b) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f)", r, g, b);
        return std::string(buf);
    };

    std::clog << "  Material: \"" << mtl.name << "\"" << std::endl;
    std::clog << "    Kd=" << fmt_color(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2])
              << " Ks=" << fmt_color(mtl.specular[0], mtl.specular[1], mtl.specular[2])
              << " Ke=" << fmt_color(mtl.emission[0], mtl.emission[1], mtl.emission[2])
              << std::endl;
    std::clog << "    Ns=" << mtl.shininess << " Ni=" << mtl.ior << " d=" << mtl.dissolve
              << " illum=" << mtl.illum << std::endl;
    std::clog << "    Pm=" << mtl.metallic << " Pr=" << mtl.roughness << std::endl;

    // 1. EMISSION - emissive materials become lights
    double emission_intensity = mtl.emission[0] + mtl.emission[1] + mtl.emission[2];
    if (emission_intensity > 0.001) {
        std::clog << "    -> diffuse_light (emission detected)" << std::endl;
        return make_shared<diffuse_light>(color(mtl.emission[0], mtl.emission[1], mtl.emission[2]));
    }

    // 2. PBR METALLIC - Blender's Principled BSDF metallic workflow
    if (mtl.metallic >= 0.5) {
        color albedo(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);
        double fuzz = mtl.roughness * 0.5;
        fuzz = std::max(0.0, std::min(1.0, fuzz));
        std::clog << "    -> metal (PBR metallic=" << mtl.metallic << ", fuzz=" << fuzz << ")"
                  << std::endl;
        return make_shared<metal>(albedo, fuzz);
    }

    // 3. TRANSPARENCY - only when explicitly transparent
    bool is_glass_illum = (mtl.illum == 4 || mtl.illum == 6 || mtl.illum == 7 || mtl.illum == 9);
    if (mtl.dissolve < 0.99 || is_glass_illum) {
        double ior = (mtl.ior > 1.0) ? mtl.ior : 1.5;
        std::clog << "    -> dielectric (dissolve=" << mtl.dissolve << ", illum=" << mtl.illum
                  << ", ior=" << ior << ")" << std::endl;
        return make_shared<dielectric>(ior);
    }

    // 4. TRADITIONAL SPECULAR METAL (fallback for non-PBR OBJ files)
    double spec_intensity = (mtl.specular[0] + mtl.specular[1] + mtl.specular[2]) / 3.0;
    if (spec_intensity > 0.5 && mtl.metallic < 0.001) {
        double fuzz = 1.0 - std::min(1.0, mtl.shininess / 1000.0);
        fuzz = std::max(0.0, std::min(0.5, fuzz));
        color albedo(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);
        std::clog << "    -> metal (traditional specular=" << spec_intensity << ", fuzz=" << fuzz
                  << ")" << std::endl;
        return make_shared<metal>(albedo, fuzz);
    }

    // 5. DEFAULT - lambertian diffuse material
    // Check if there's a diffuse texture
    if (!mtl.diffuse_texname.empty()) {
        std::string tex_path = base_path;
        if (!tex_path.empty() && tex_path.back() != '/') {
            tex_path += "/";
        }
        tex_path += mtl.diffuse_texname;
        std::clog << "    -> lambertian with texture: " << tex_path << std::endl;
        auto tex = make_shared<image_texture>(tex_path.c_str());
        return make_shared<lambertian>(tex);
    }

    // Solid color lambertian
    color diffuse_color(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);

    // If diffuse is black/zero, use a default gray
    if (diffuse_color.length_squared() < 0.001) {
        diffuse_color = color(0.5, 0.5, 0.5);
        std::clog << "    -> lambertian (default gray, Kd was near-zero)" << std::endl;
    } else {
        std::clog << "    -> lambertian (diffuse)" << std::endl;
    }

    return make_shared<lambertian>(diffuse_color);
}

// Load an OBJ file and return a hittable (BVH of triangles)
// Parameters:
//   filename: Path to the .obj file
//   scale: Scale factor for each axis (default 1,1,1)
//   translate_offset: Translation to apply after scaling (default 0,0,0)
//   rotate_y_angle: Rotation around Y axis in degrees (default 0)
//   default_mat: If provided, overrides all materials from the OBJ file
inline shared_ptr<hittable> load_obj(const std::string& filename, const vec3& scale = vec3(1, 1, 1),
                                     const vec3& translate_offset = vec3(0, 0, 0),
                                     double rotate_y_angle = 0.0,
                                     shared_ptr<material> default_mat = nullptr) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // Extract base path for material/texture loading
    std::string base_path = "";
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        base_path = filename.substr(0, last_slash);
    }

    // Load the OBJ file (triangulate = true to ensure all faces are triangles)
    bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(),
                                    base_path.empty() ? nullptr : base_path.c_str(),
                                    true  // triangulate
    );

    if (!warn.empty()) {
        std::cerr << "OBJ Loader Warning: " << warn << std::endl;
    }

    if (!err.empty()) {
        std::cerr << "OBJ Loader Error: " << err << std::endl;
    }

    if (!success) {
        std::cerr << "Failed to load OBJ file: " << filename << std::endl;
        // Return an empty hittable_list wrapped in BVH
        return make_shared<bvh_node>(hittable_list());
    }

    // Convert OBJ materials to raytracer materials
    std::vector<shared_ptr<material>> converted_materials;
    for (const auto& mtl : materials) {
        converted_materials.push_back(convert_obj_material(mtl, base_path));
    }

    // Default material if none provided and OBJ has no materials
    auto fallback_mat = default_mat ? default_mat : make_shared<lambertian>(color(0.5, 0.5, 0.5));

    // Collect all triangles
    hittable_list triangles;

    for (const auto& shape : shapes) {
        size_t index_offset = 0;

        // Iterate over faces
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            size_t fv = shape.mesh.num_face_vertices[f];

            // We requested triangulation, so fv should always be 3
            if (fv != 3) {
                std::cerr << "Warning: Non-triangle face encountered (skipping)" << std::endl;
                index_offset += fv;
                continue;
            }

            // Get vertex indices for this face
            tinyobj::index_t idx0 = shape.mesh.indices[index_offset + 0];
            tinyobj::index_t idx1 = shape.mesh.indices[index_offset + 1];
            tinyobj::index_t idx2 = shape.mesh.indices[index_offset + 2];

            // Extract and scale vertex positions
            point3 v0(attrib.vertices[3 * idx0.vertex_index + 0] * scale.x(),
                      attrib.vertices[3 * idx0.vertex_index + 1] * scale.y(),
                      attrib.vertices[3 * idx0.vertex_index + 2] * scale.z());
            point3 v1(attrib.vertices[3 * idx1.vertex_index + 0] * scale.x(),
                      attrib.vertices[3 * idx1.vertex_index + 1] * scale.y(),
                      attrib.vertices[3 * idx1.vertex_index + 2] * scale.z());
            point3 v2(attrib.vertices[3 * idx2.vertex_index + 0] * scale.x(),
                      attrib.vertices[3 * idx2.vertex_index + 1] * scale.y(),
                      attrib.vertices[3 * idx2.vertex_index + 2] * scale.z());

            // Get material for this face
            shared_ptr<material> face_mat;
            if (default_mat) {
                face_mat = default_mat;
            } else {
                int mat_id = shape.mesh.material_ids[f];
                if (mat_id >= 0 && mat_id < static_cast<int>(converted_materials.size())) {
                    face_mat = converted_materials[mat_id];
                } else {
                    face_mat = fallback_mat;
                }
            }

            // Check if we have normals
            bool has_normals =
                (idx0.normal_index >= 0) && (idx1.normal_index >= 0) && (idx2.normal_index >= 0);

            // Check if we have texture coordinates
            bool has_texcoords = (idx0.texcoord_index >= 0) && (idx1.texcoord_index >= 0) &&
                                 (idx2.texcoord_index >= 0);

            if (has_normals && has_texcoords) {
                // Full constructor with normals and UVs
                vec3 n0(attrib.normals[3 * idx0.normal_index + 0],
                        attrib.normals[3 * idx0.normal_index + 1],
                        attrib.normals[3 * idx0.normal_index + 2]);
                vec3 n1(attrib.normals[3 * idx1.normal_index + 0],
                        attrib.normals[3 * idx1.normal_index + 1],
                        attrib.normals[3 * idx1.normal_index + 2]);
                vec3 n2(attrib.normals[3 * idx2.normal_index + 0],
                        attrib.normals[3 * idx2.normal_index + 1],
                        attrib.normals[3 * idx2.normal_index + 2]);

                vec3 uv0(attrib.texcoords[2 * idx0.texcoord_index + 0],
                         attrib.texcoords[2 * idx0.texcoord_index + 1], 0);
                vec3 uv1(attrib.texcoords[2 * idx1.texcoord_index + 0],
                         attrib.texcoords[2 * idx1.texcoord_index + 1], 0);
                vec3 uv2(attrib.texcoords[2 * idx2.texcoord_index + 0],
                         attrib.texcoords[2 * idx2.texcoord_index + 1], 0);

                triangles.add(
                    make_shared<triangle>(v0, v1, v2, face_mat, n0, n1, n2, uv0, uv1, uv2));
            } else if (has_normals) {
                // Constructor with normals only
                vec3 n0(attrib.normals[3 * idx0.normal_index + 0],
                        attrib.normals[3 * idx0.normal_index + 1],
                        attrib.normals[3 * idx0.normal_index + 2]);
                vec3 n1(attrib.normals[3 * idx1.normal_index + 0],
                        attrib.normals[3 * idx1.normal_index + 1],
                        attrib.normals[3 * idx1.normal_index + 2]);
                vec3 n2(attrib.normals[3 * idx2.normal_index + 0],
                        attrib.normals[3 * idx2.normal_index + 1],
                        attrib.normals[3 * idx2.normal_index + 2]);

                triangles.add(make_shared<triangle>(v0, v1, v2, face_mat, n0, n1, n2));
            } else {
                // Basic constructor (flat shading)
                triangles.add(make_shared<triangle>(v0, v1, v2, face_mat));
            }

            index_offset += fv;
        }
    }

    std::clog << "Loaded OBJ: " << filename << " with " << triangles.objects.size() << " triangles"
              << std::endl;

    // Build BVH from all triangles
    shared_ptr<hittable> result = make_shared<bvh_node>(triangles);

    // Apply rotation if specified
    if (std::fabs(rotate_y_angle) > 0.001) {
        result = make_shared<rotate_y>(result, rotate_y_angle);
    }

    // Apply translation if specified
    if (translate_offset.length_squared() > 0.000001) {
        result = make_shared<translate>(result, translate_offset);
    }

    return result;
}

#endif
