#include "io/scene_loader.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnan-infinity-disabled"
#include <nlohmann/json.hpp>
#pragma clang diagnostic pop
#include <stdexcept>
#include <string>
#include <vector>

#include "core/spectral/spectral_curve.h"
#include "core/spectral/spectral_utils.h"
#include "core/transform.h"
#include "core/vec3.h"
#include "geometry/sphere.h"
#include "io/obj_loader.h"
#include "materials/material.h"
#include "scene/mesh_utils.h"
#include "scene/scene.h"
#include "session/render_options.h"

using json = nlohmann::json;

namespace skwr {

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

static Vec3 ParseVec3(const json& j) {
    if (!j.is_array() || j.size() != 3) {
        throw std::runtime_error("Expected array of 3 numbers for Vec3");
    }
    return Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

static RGB ParseRGB(const json& j) {
    if (!j.is_array() || j.size() != 3) {
        throw std::runtime_error("Expected array of 3 numbers for Spectrum");
    }
    return RGB(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

template <typename T>
static T GetOr(const json& j, const std::string& key, const T& default_value) {
    if (j.contains(key)) {
        return j[key].get<T>();
    }
    return default_value;
}

static Vec3 GetVec3Or(const json& j, const std::string& key, const Vec3& default_value) {
    if (j.contains(key)) {
        return ParseVec3(j[key]);
    }
    return default_value;
}

static RGB GetRGBOr(const json& j, const std::string& key, const RGB& default_value) {
    if (j.contains(key)) {
        return ParseRGB(j[key]);
    }
    return default_value;
}

//------------------------------------------------------------------------------
// Material Parsing
//------------------------------------------------------------------------------

using MaterialMap = std::map<std::string, uint32_t>;

static MaterialMap ParseMaterials(const json& j, Scene& scene) {
    MaterialMap mat_map;

    if (!j.contains("materials")) {
        return mat_map;
    }

    const auto& mats = j["materials"];
    for (auto it = mats.begin(); it != mats.end(); ++it) {
        const std::string& name = it.key();
        const json& m = it.value();

        Material mat{};
        std::string type = m.at("type").get<std::string>();

        if (type == "lambertian") {
            mat.type = MaterialType::Lambertian;
            mat.albedo = RGBToCurve(GetRGBOr(m, "albedo", RGB(1.0f)));
        } else if (type == "metal") {
            mat.type = MaterialType::Metal;
            mat.albedo = RGBToCurve(GetRGBOr(m, "albedo", RGB(1.0f)));
            mat.roughness = GetOr(m, "roughness", 0.0f);
        } else if (type == "dielectric") {
            mat.type = MaterialType::Dielectric;
            mat.albedo = RGBToCurve(GetRGBOr(m, "albedo", RGB(1.0f)));
            mat.ior = m.at("ior").get<float>();
            mat.roughness = GetOr(m, "roughness", 0.0f);
        } else {
            throw std::runtime_error("Unknown material type: " + type + " (material '" + name +
                                     "')");
        }

        // Optional emission and opacity (apply to any material type)
        mat.emission = RGBToCurve(GetRGBOr(m, "emission", RGB(0.0f)));
        mat.opacity = RGBToCurve(GetRGBOr(m, "opacity", RGB(1.0f)));

        uint32_t id = scene.AddMaterial(mat);
        mat_map[name] = id;

        std::clog << "[Scene] Material '" << name << "' -> " << type << " (id=" << id << ")"
                  << std::endl;
    }

    return mat_map;
}

//------------------------------------------------------------------------------
// Object Parsing
//------------------------------------------------------------------------------

static uint32_t LookupMaterial(const json& obj, const MaterialMap& mat_map, int obj_index) {
    if (!obj.contains("material")) {
        throw std::runtime_error("Object at index " + std::to_string(obj_index) +
                                 " missing 'material' field");
    }

    std::string mat_name = obj["material"].get<std::string>();
    auto it = mat_map.find(mat_name);
    if (it == mat_map.end()) {
        throw std::runtime_error("Object at index " + std::to_string(obj_index) +
                                 ": unknown material '" + mat_name + "'");
    }
    return it->second;
}

static void ParseSphere(const json& obj, const MaterialMap& mat_map, Scene& scene, int index) {
    uint32_t mat_id = LookupMaterial(obj, mat_map, index);
    Vec3 center = ParseVec3(obj.at("center"));
    float radius = obj.at("radius").get<float>();

    scene.AddSphere(Sphere{center, radius, mat_id});
    std::clog << "[Scene] Sphere at (" << center << "), r=" << radius << std::endl;
}

static void ParseQuad(const json& obj, const MaterialMap& mat_map, Scene& scene, int index) {
    uint32_t mat_id = LookupMaterial(obj, mat_map, index);

    const auto& verts = obj.at("vertices");
    if (!verts.is_array() || verts.size() != 4) {
        throw std::runtime_error("Object at index " + std::to_string(index) +
                                 ": quad 'vertices' must be array of 4 points");
    }

    Vec3 p0 = ParseVec3(verts[0]);
    Vec3 p1 = ParseVec3(verts[1]);
    Vec3 p2 = ParseVec3(verts[2]);
    Vec3 p3 = ParseVec3(verts[3]);

    scene.AddMesh(CreateQuad(p0, p1, p2, p3, mat_id));

    std::string comment = GetOr<std::string>(obj, "comment", "");
    if (!comment.empty()) {
        std::clog << "[Scene] Quad: " << comment << std::endl;
    } else {
        std::clog << "[Scene] Quad" << std::endl;
    }
}

static void ParseObj(const json& obj, const MaterialMap& mat_map, Scene& scene, int index,
                     const std::string& scene_dir) {
    std::string file = obj.at("file").get<std::string>();

    // Resolve path relative to scene file directory
    std::string filepath;
    if (!file.empty() && file[0] == '/') {
        filepath = file;  // Absolute path
    } else {
        filepath = scene_dir.empty() ? file : (scene_dir + "/" + file);
    }

    bool auto_fit = GetOr(obj, "auto_fit", true);

    // Parse transform
    Vec3 translate(0.0f, 0.0f, 0.0f);
    Vec3 rotate_deg(0.0f, 0.0f, 0.0f);
    Vec3 obj_scale(1.0f, 1.0f, 1.0f);

    if (obj.contains("transform")) {
        const auto& t = obj["transform"];
        translate = GetVec3Or(t, "translate", Vec3(0.0f, 0.0f, 0.0f));
        rotate_deg = GetVec3Or(t, "rotate", Vec3(0.0f, 0.0f, 0.0f));

        // Scale can be a scalar or a [x,y,z] array
        if (t.contains("scale")) {
            if (t["scale"].is_number()) {
                float s = t["scale"].get<float>();
                obj_scale = Vec3(s, s, s);
            } else {
                obj_scale = ParseVec3(t["scale"]);
            }
        }
    }

    // Record mesh count before loading so we can apply transforms to new meshes
    size_t mesh_count_before = scene.MeshCount();

    // Load OBJ — when auto_fit is true, the loader normalizes to 2-unit cube.
    // We pass Vec3(1,1,1) as scale here because we handle scaling ourselves via ApplyTransform.
    if (!LoadOBJ(filepath, scene, Vec3(1.0f, 1.0f, 1.0f), auto_fit)) {
        throw std::runtime_error("Object at index " + std::to_string(index) +
                                 ": failed to load OBJ file '" + filepath + "'");
    }

    // Override material if specified
    if (obj.contains("material") && !obj["material"].is_null()) {
        uint32_t mat_id = LookupMaterial(obj, mat_map, index);
        for (size_t i = mesh_count_before; i < scene.MeshCount(); i++) {
            scene.GetMutableMesh(static_cast<uint32_t>(i)).material_id = mat_id;
        }
    }

    // Apply transform (Scale -> Rotate -> Translate) to all newly added meshes
    bool has_transform =
        (obj_scale.x() != 1.0f || obj_scale.y() != 1.0f || obj_scale.z() != 1.0f ||
         rotate_deg.x() != 0.0f || rotate_deg.y() != 0.0f || rotate_deg.z() != 0.0f ||
         translate.x() != 0.0f || translate.y() != 0.0f || translate.z() != 0.0f);

    if (has_transform) {
        for (size_t i = mesh_count_before; i < scene.MeshCount(); i++) {
            Mesh& mesh = scene.GetMutableMesh(static_cast<uint32_t>(i));
            ApplyTransform(mesh.p, translate, rotate_deg, obj_scale);
            if (!mesh.n.empty()) {
                ApplyRotationToNormals(mesh.n, rotate_deg);
            }
        }
    }

    std::clog << "[Scene] OBJ: " << filepath << " (auto_fit=" << auto_fit << ")" << std::endl;
}

static void ParseObjects(const json& j, const MaterialMap& mat_map, Scene& scene,
                         const std::string& scene_dir) {
    if (!j.contains("objects")) {
        return;
    }

    const auto& objects = j["objects"];
    for (int i = 0; i < static_cast<int>(objects.size()); i++) {
        const auto& obj = objects[i];
        std::string type = obj.at("type").get<std::string>();

        if (type == "sphere") {
            ParseSphere(obj, mat_map, scene, i);
        } else if (type == "quad") {
            ParseQuad(obj, mat_map, scene, i);
        } else if (type == "obj") {
            ParseObj(obj, mat_map, scene, i, scene_dir);
        } else {
            throw std::runtime_error("Object at index " + std::to_string(i) + ": unknown type '" +
                                     type + "'");
        }
    }
}

//------------------------------------------------------------------------------
// Camera & Render Config Parsing
//------------------------------------------------------------------------------

static SceneConfig ParseConfig(const json& j) {
    SceneConfig config{};

    // --- Camera ---
    if (!j.contains("camera")) {
        throw std::runtime_error("Scene file missing 'camera' section");
    }

    const auto& cam = j["camera"];
    config.look_from = ParseVec3(cam.at("look_from"));
    config.look_at = ParseVec3(cam.at("look_at"));
    config.vup = GetVec3Or(cam, "vup", Vec3(0.0f, 1.0f, 0.0f));
    config.vfov = GetOr(cam, "vfov", 90.0f);

    // --- Render ---
    auto& opts = config.render_options;

    // Defaults
    opts.integrator_type = IntegratorType::PathTrace;
    opts.integrator_config.samples_per_pixel = 200;
    opts.integrator_config.start_sample = 0;
    opts.integrator_config.max_depth = 50;
    opts.integrator_config.num_threads = 0;
    opts.integrator_config.enable_deep = false;
    opts.image_config.width = 800;
    opts.image_config.height = 450;
    opts.image_config.outfile = "output.ppm";
    opts.image_config.exrfile = "output.exr";

    if (j.contains("render")) {
        const auto& r = j["render"];

        // Integrator type
        std::string integrator_str = GetOr<std::string>(r, "integrator", "path_trace");
        if (integrator_str == "path_trace") {
            opts.integrator_type = IntegratorType::PathTrace;
        } else if (integrator_str == "normals") {
            opts.integrator_type = IntegratorType::Normals;
        } else {
            throw std::runtime_error("Unknown integrator type: " + integrator_str);
        }

        opts.integrator_config.samples_per_pixel = GetOr(r, "samples_per_pixel", 200);
        opts.integrator_config.max_depth = GetOr(r, "max_depth", 50);
        opts.integrator_config.num_threads = GetOr(r, "threads", 0);
        opts.integrator_config.enable_deep = GetOr(r, "enable_deep", false);

        // Image config (nested)
        if (r.contains("image")) {
            const auto& img = r["image"];
            opts.image_config.width = GetOr(img, "width", 800);
            opts.image_config.height = GetOr(img, "height", 450);
            opts.image_config.outfile = GetOr<std::string>(img, "outfile", "output.ppm");
            opts.image_config.exrfile = GetOr<std::string>(img, "exrfile", "output.exr");
        }
    }

    return config;
}

//------------------------------------------------------------------------------
// Main Entry Point
//------------------------------------------------------------------------------

SceneConfig LoadSceneFile(const std::string& filepath, Scene& scene) {
    std::clog << "[Scene] Loading scene file: " << filepath << std::endl;

    // Open and parse JSON
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open scene file: " + filepath);
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("JSON parse error in '" + filepath +
                                 "': " + std::string(e.what()));
    }

    // Extract scene file directory for resolving relative paths
    std::string scene_dir;
    size_t last_slash = filepath.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        scene_dir = filepath.substr(0, last_slash);
    }

    // 1. Parse materials first (objects reference them by name)
    MaterialMap mat_map = ParseMaterials(j, scene);

    // 2. Parse objects (geometry)
    ParseObjects(j, mat_map, scene, scene_dir);

    // 3. Parse camera and render config
    SceneConfig config = ParseConfig(j);

    std::clog << "[Scene] Scene loaded successfully" << std::endl;
    return config;
}

}  // namespace skwr
