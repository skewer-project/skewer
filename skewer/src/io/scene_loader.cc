#include "io/scene_loader.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/cpu_config.h"
#include "core/math/transform.h"
#include "core/math/vec3.h"
#include "core/spectral/spectral_curve.h"
#include "core/spectral/spectral_utils.h"
#include "geometry/sphere.h"
#include "io/graph_from_json.h"
#include "io/obj_loader.h"
#include "materials/material.h"
#include "materials/texture.h"
#include "media/mediums.h"
#include "media/nano_vdb_medium.h"
#include "scene/mesh_utils.h"
#include "scene/scene.h"
#include "scene/scene_graph.h"
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

static std::string ExtractDir(const std::string& filepath) {
    size_t last_slash = filepath.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        return filepath.substr(0, last_slash);
    }
    return "";
}

static std::string ResolvePath(const std::string& path, const std::string& base_dir) {
    if (!path.empty() && path[0] == '/') return path;
    return base_dir.empty() ? path : (base_dir + "/" + path);
}

// Media Parsing
using MediaMap = std::map<std::string, uint16_t>;

static MediaMap ParseMedia(const json& j, Scene& scene, const std::string& scene_dir) {
    MediaMap media_map;

    if (!j.contains("media")) {
        return media_map;
    }

    const auto& media = j["media"];

    for (auto it = media.begin(); it != media.end(); ++it) {
        const std::string& name = it.key();
        const json& m = it.value();

        std::string type = m.at("type").get<std::string>();

        if (type == "nanovdb") {
            NanoVDBMedium med;

            med.sigma_a_base = RGBToCurve(GetRGBOr(m, "sigma_a", RGB(0.0f)));
            med.sigma_s_base = RGBToCurve(GetRGBOr(m, "sigma_s", RGB(0.0f)));

            med.g = GetOr(m, "g", 0.0f);
            med.density_multiplier = GetOr(m, "density_multiplier", 1.0f);

            // Spatial overrides
            med.scale = GetOr(m, "scale", 1.0f);
            med.translate = GetVec3Or(m, "translate", Vec3(0.0f, 0.0f, 0.0f));

            std::string file = m.at("file").get<std::string>();
            std::string filepath = ResolvePath(file, scene_dir);

            if (!med.Load(filepath)) {
                throw std::runtime_error("Failed to load NanoVDB: " + filepath);
            }

            uint16_t id = scene.AddNanoVDBMedium(std::move(med));
            media_map[name] = id;
        } else {
            throw std::runtime_error("Unknown medium type: " + type);
        }
    }

    return media_map;
}

//------------------------------------------------------------------------------
// Material Parsing
//------------------------------------------------------------------------------

using MaterialMap = std::map<std::string, uint32_t>;

// Helper: load a texture from a path string.
// Resolves relative paths against scene_dir.
// Returns kNoTexture if the string is empty or load fails.
static uint32_t LoadSceneTexture(const json& m, const std::string& key,
                                 const std::string& scene_dir, Scene& scene) {
    if (!m.contains(key)) return kNoTexture;

    std::string texpath = m[key].get<std::string>();
    if (texpath.empty()) return kNoTexture;

    std::string filepath = ResolvePath(texpath, scene_dir);

    ImageTexture tex;
    if (!tex.Load(filepath)) return kNoTexture;

    return scene.AddTexture(std::move(tex));
}

// layer_visible: if false, forces mat.visible = false on all materials regardless of per-mat JSON.
static MaterialMap ParseMaterials(const json& j, Scene& scene, const std::string& scene_dir,
                                  bool layer_visible = true) {
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
        // Layer-level visibility overrides per-material visibility
        mat.visible = layer_visible ? GetOr(m, "visible", true) : false;

        // Optional texture maps (paths resolved relative to scene file directory)
        mat.albedo_tex = LoadSceneTexture(m, "albedo_texture", scene_dir, scene);
        mat.normal_tex = LoadSceneTexture(m, "normal_texture", scene_dir, scene);
        mat.roughness_tex = LoadSceneTexture(m, "roughness_texture", scene_dir, scene);

        uint32_t id = scene.AddMaterial(mat);
        mat_map[name] = id;
    }

    return mat_map;
}

//------------------------------------------------------------------------------
// Graph parsing
//------------------------------------------------------------------------------

static uint16_t LookupMedium(const json& obj, const MediaMap& media_map, const std::string& key) {
    if (!obj.contains(key)) return static_cast<uint16_t>(MediumType::Vacuum);

    std::string name = obj[key].get<std::string>();

    auto it = media_map.find(name);
    if (it == media_map.end()) {
        throw std::runtime_error("Unknown medium '" + name + "'");
    }

    return it->second;
}

static uint32_t LookupMaterial(const json& obj, const MaterialMap& mat_map, int obj_index) {
    if (!obj.contains("material")) {
        throw std::runtime_error("Object at index " + std::to_string(obj_index) +
                                 " missing 'material' field");
    }

    std::string mat_name = obj["material"].get<std::string>();

    if (mat_name == "null" || mat_name == "none") return kNullMaterialId;

    auto it = mat_map.find(mat_name);
    if (it == mat_map.end()) {
        throw std::runtime_error("Object at index " + std::to_string(obj_index) +
                                 ": unknown material '" + mat_name + "'");
    }
    return it->second;
}

// Returns the material ID for an object, applying any object-level "visible"
// override.  When the object's visibility differs from the base material's,
// a one-off clone is registered in the scene so other objects sharing that
// material are not affected.
static uint32_t LookupMaterialWithVisibility(const json& obj, const MaterialMap& mat_map,
                                             Scene& scene, int obj_index) {
    uint32_t mat_id = LookupMaterial(obj, mat_map, obj_index);
    if (!obj.contains("visible")) return mat_id;

    bool want_visible = obj["visible"].get<bool>();
    // Copy before potentially invalidating the reference via AddMaterial.
    Material cloned = scene.GetMaterial(mat_id);
    if (cloned.visible == want_visible) return mat_id;

    cloned.visible = want_visible;
    return scene.AddMaterial(cloned);
}

static void LoadObjMeshes(const json& obj, const MaterialMap& mat_map, Scene& scene, int index,
                          const std::string& scene_dir) {
    std::string file = obj.at("file").get<std::string>();
    std::string filepath = ResolvePath(file, scene_dir);

    bool auto_fit = GetOr(obj, "auto_fit", true);

    size_t mesh_count_before = scene.MeshCount();

    if (!LoadOBJ(filepath, scene, Vec3(1.0f, 1.0f, 1.0f), auto_fit)) {
        throw std::runtime_error("Graph node " + std::to_string(index) + ": failed to load OBJ '" +
                                 filepath + "'");
    }

    if (obj.contains("material") && !obj["material"].is_null()) {
        uint32_t mat_id = LookupMaterialWithVisibility(obj, mat_map, scene, index);
        for (size_t i = mesh_count_before; i < scene.MeshCount(); i++) {
            scene.GetMutableMesh(static_cast<uint32_t>(i)).material_id = mat_id;
        }
    } else if (obj.contains("visible")) {
        bool want_visible = obj["visible"].get<bool>();
        std::unordered_map<uint32_t, uint32_t> vis_cache;
        for (size_t i = mesh_count_before; i < scene.MeshCount(); i++) {
            Mesh& mesh = scene.GetMutableMesh(static_cast<uint32_t>(i));
            uint32_t orig = mesh.material_id;
            auto it = vis_cache.find(orig);
            if (it != vis_cache.end()) {
                mesh.material_id = it->second;
            } else {
                Material cloned = scene.GetMaterial(orig);
                uint32_t new_id = orig;
                if (cloned.visible != want_visible) {
                    cloned.visible = want_visible;
                    new_id = scene.AddMaterial(cloned);
                }
                vis_cache[orig] = new_id;
                mesh.material_id = new_id;
            }
        }
    }
}

static SceneNode ParseGraphNode(const json& j, const MaterialMap& mat_map,
                                const MediaMap& media_map, Scene& scene,
                                const std::string& scene_dir, const std::string& path_label) {
    SceneNode node;

    if (j.contains("name") && j["name"].is_string()) {
        node.name = j["name"].get<std::string>();
    }

    if (j.contains("transform")) {
        node.anim_transform = ParseAnimatedTransformJson(j["transform"]);
    } else {
        node.anim_transform = ParseAnimatedTransformJson(json::object());
    }

    if (j.contains("children")) {
        if (!j["children"].is_array()) {
            throw std::runtime_error("Graph node " + path_label + ": 'children' must be an array");
        }
        node.type = NodeType::Group;
        int ci = 0;
        for (const auto& ch : j["children"]) {
            node.children.push_back(
                ParseGraphNode(ch, mat_map, media_map, scene, scene_dir,
                               path_label + ".children[" + std::to_string(ci++) + "]"));
        }
        return node;
    }

    std::string typ = j.at("type").get<std::string>();

    if (typ == "sphere") {
        node.type = NodeType::Sphere;
        uint32_t mat_id = LookupMaterialWithVisibility(j, mat_map, scene, -1);

        uint16_t inside = LookupMedium(j, media_map, "inside_medium");
        uint16_t outside = LookupMedium(j, media_map, "outside_medium");

        SphereData sd{};
        sd.material_id = mat_id;
        sd.light_index = -1;
        sd.interior_medium = inside;
        sd.exterior_medium = outside;
        sd.priority = 1;

        uint16_t med_index = ExtractMediumIndex(inside);
        MediumType med_type = ExtractMediumType(inside);

        if (inside != kVacuumMediumId && med_type == MediumType::NanoVDB) {
            const NanoVDBMedium& medium = scene.nanovdb_media()[med_index];
            sd.center = medium.Center();
            sd.radius = medium.BoundingRadius() * 1.05f;
            sd.center_is_world = true;
        } else {
            sd.center = ParseVec3(j.at("center"));
            sd.radius = j.at("radius").get<float>();
            sd.center_is_world = false;
        }
        node.sphere_data = sd;
        return node;
    }

    if (typ == "quad") {
        node.type = NodeType::Mesh;
        uint32_t mat_id = LookupMaterialWithVisibility(j, mat_map, scene, -1);

        const auto& verts = j.at("vertices");
        if (!verts.is_array() || verts.size() != 4) {
            throw std::runtime_error("Graph node " + path_label +
                                     ": quad 'vertices' must be array of 4 points");
        }

        Vec3 p0 = ParseVec3(verts[0]);
        Vec3 p1 = ParseVec3(verts[1]);
        Vec3 p2 = ParseVec3(verts[2]);
        Vec3 p3 = ParseVec3(verts[3]);

        uint32_t mid = scene.AddMesh(CreateQuad(p0, p1, p2, p3, mat_id));
        node.mesh_ids.push_back(mid);
        return node;
    }

    if (typ == "obj") {
        node.type = NodeType::Mesh;
        size_t mesh_count_before = scene.MeshCount();
        LoadObjMeshes(j, mat_map, scene, -1, scene_dir);
        for (size_t i = mesh_count_before; i < scene.MeshCount(); i++) {
            node.mesh_ids.push_back(static_cast<uint32_t>(i));
        }
        return node;
    }

    throw std::runtime_error("Graph node " + path_label + ": unknown type '" + typ + "'");
}

static void ParseGraph(const json& j, const MaterialMap& mat_map, const MediaMap& media_map,
                       Scene& scene, const std::string& scene_dir, const std::string& filepath) {
    if (!j.contains("graph")) {
        throw std::runtime_error("Layer file must use 'graph' format: " + filepath);
    }
    const auto& g = j["graph"];
    if (!g.is_array()) {
        throw std::runtime_error("'graph' must be an array in: " + filepath);
    }
    std::vector<SceneNode> roots;
    roots.reserve(g.size());
    for (size_t i = 0; i < g.size(); i++) {
        roots.push_back(ParseGraphNode(g[i], mat_map, media_map, scene, scene_dir,
                                       "graph[" + std::to_string(i) + "]"));
    }
    scene.MergeGraphRoots(std::move(roots));
}

//------------------------------------------------------------------------------
// Render Options Parsing
//------------------------------------------------------------------------------

static RenderOptions ParseRenderOptions(const json& j) {
    RenderOptions opts{};

    // Defaults
    opts.integrator_type = IntegratorType::PathTrace;
    opts.integrator_config.max_samples = 200;
    opts.integrator_config.start_sample = 0;
    opts.integrator_config.max_depth = 50;
    opts.integrator_config.num_threads = 0;
    opts.integrator_config.enable_deep = false;
    opts.image_config.width = 800;
    opts.image_config.height = 450;
    opts.image_config.outfile = "output.png";
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

        // Accept both "max_samples" and legacy "samples_per_pixel"
        opts.integrator_config.max_samples =
            GetOr(r, "max_samples", GetOr(r, "samples_per_pixel", 200));
        opts.integrator_config.max_depth = GetOr(r, "max_depth", 50);
        opts.integrator_config.num_threads = GetOr(r, "threads", 0);
        opts.integrator_config.enable_deep = GetOr(r, "enable_deep", false);
        opts.integrator_config.transparent_background = GetOr(r, "transparent_background", false);
        opts.integrator_config.visibility_depth = GetOr(r, "visibility_depth", 1);
        opts.integrator_config.tile_size = GetOr(r, "tile_size", 32);

        // Adaptive sampling
        opts.integrator_config.noise_threshold = GetOr(r, "noise_threshold", 0.0f);
        opts.integrator_config.min_samples = GetOr(r, "min_samples", 1);
        opts.integrator_config.adaptive_step = GetOr(r, "adaptive_step", 16);
        opts.integrator_config.save_sample_map = GetOr(r, "save_sample_map", false);

        // Image config (nested)
        if (r.contains("image")) {
            const auto& img = r["image"];
            opts.image_config.width = GetOr(img, "width", 800);
            opts.image_config.height = GetOr(img, "height", 450);
            opts.image_config.outfile = GetOr<std::string>(img, "outfile", "output.png");
            opts.image_config.exrfile = GetOr<std::string>(img, "exrfile", "output.exr");
        }
    }

    return opts;
}

//------------------------------------------------------------------------------
// Open + Parse JSON helper
//------------------------------------------------------------------------------

static json OpenJSON(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("JSON parse error in '" + filepath +
                                 "': " + std::string(e.what()));
    }
    return j;
}

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

SceneConfig LoadSceneFile(const std::string& filepath) {
    json j = OpenJSON(filepath);

    if (!j.contains("camera")) {
        throw std::runtime_error("Scene file missing 'camera' section: " + filepath);
    }
    if (!j.contains("layers")) {
        throw std::runtime_error("Scene file missing 'layers' section: " + filepath);
    }

    std::string scene_dir = ExtractDir(filepath);

    SceneConfig config{};

    // Parse camera
    const auto& cam = j["camera"];
    config.look_from = ParseVec3(cam.at("look_from"));
    config.look_at = ParseVec3(cam.at("look_at"));
    config.vup = GetVec3Or(cam, "vup", Vec3(0.0f, 1.0f, 0.0f));
    config.vfov = GetOr(cam, "vfov", 90.0f);
    config.aperture_radius = GetOr(cam, "aperture_radius", 0.0f);
    config.focus_distance = GetOr(cam, "focus_distance", 1.0f);
    config.shutter_open = GetOr(cam, "shutter_open", 0.0f);
    config.shutter_close = GetOr(cam, "shutter_close", 0.0f);

    // Output directory (local path or cloud URI — used as-is, not resolved)
    config.output_dir = GetOr<std::string>(j, "output_dir", "");

    // Resolve context paths
    if (j.contains("context")) {
        for (const auto& p : j["context"]) {
            config.context_paths.push_back(ResolvePath(p.get<std::string>(), scene_dir));
        }
    }

    // Resolve layer paths
    for (const auto& p : j["layers"]) {
        config.layer_paths.push_back(ResolvePath(p.get<std::string>(), scene_dir));
    }

    return config;
}

LayerConfig LoadLayerFile(const std::string& filepath, Scene& scene) {
    json j = OpenJSON(filepath);

    if (j.contains("camera")) {
        throw std::runtime_error("Layer file must not contain a 'camera' key: " + filepath);
    }

    std::string scene_dir = ExtractDir(filepath);

    // Layer-level visibility: if false, all materials in this layer become invisible
    bool layer_visible = GetOr(j, "visible", true);

    MaterialMap mat_map = ParseMaterials(j, scene, scene_dir, layer_visible);
    MediaMap media_map = ParseMedia(j, scene, scene_dir);
    ParseGraph(j, mat_map, media_map, scene, scene_dir, filepath);

    LayerConfig lcfg{};
    lcfg.visible = layer_visible;
    lcfg.render_options = ParseRenderOptions(j);
    return lcfg;
}

void LoadContextIntoScene(const std::vector<std::string>& context_paths, Scene& scene) {
    for (const auto& path : context_paths) {
        json j = OpenJSON(path);

        if (j.contains("camera")) {
            throw std::runtime_error("Context file must not contain a 'camera' key: " + path);
        }

        std::string ctx_dir = ExtractDir(path);
        MaterialMap mat_map = ParseMaterials(j, scene, ctx_dir);
        MediaMap media_map = ParseMedia(j, scene, ctx_dir);
        ParseGraph(j, mat_map, media_map, scene, ctx_dir, path);
    }
}

}  // namespace skwr
