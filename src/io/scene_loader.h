#ifndef SCENE_LOADER_H
#define SCENE_LOADER_H
//==============================================================================================
// JSON Scene Loader for Ray Tracer
// Parses JSON scene files and constructs the world and camera configuration.
//==============================================================================================

#include "geometry/bvh.h"
#include "renderer/camera.h"
#include "geometry/constant_medium.h"
#include "geometry/hittable.h"
#include "renderer/scene.h"
#include "materials/material.h"
#include "io/obj_loader.h"
#include "geometry/quad.h"
#include "geometry/sphere.h"
#include "materials/texture.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

using json = nlohmann::json;


// Result of loading a scene
struct scene_data
{
    camera cam;
    hittable_list world;
};


// Type aliases for readability
using texture_map = std::map<std::string, shared_ptr<texture>>;
using material_map = std::map<std::string, shared_ptr<material>>;


//------------------------------------------------------------------------------
// Helper Functions
//------------------------------------------------------------------------------

inline vec3 parse_vec3(const json& j)
{
    if (!j.is_array() || j.size() != 3)
    {
        throw std::runtime_error("Expected array of 3 numbers for vec3");
    }
    return vec3(j[0].get<double>(), j[1].get<double>(), j[2].get<double>());
}

inline color parse_color(const json& j)
{
    return parse_vec3(j);  // Colors are just vec3
}

// Get a value with a default if not present
template<typename T>
T get_or(const json& j, const std::string& key, const T& default_value)
{
    if (j.contains(key))
    {
        return j[key].get<T>();
    }
    return default_value;
}

// Get a vec3 with a default if not present
inline vec3 get_vec3_or(const json& j, const std::string& key, const vec3& default_value)
{
    if (j.contains(key))
    {
        return parse_vec3(j[key]);
    }
    return default_value;
}


//------------------------------------------------------------------------------
// Texture Loading
//------------------------------------------------------------------------------

inline shared_ptr<texture> parse_texture(const json& j)
{
    std::string type = j.at("type").get<std::string>();

    if (type == "solid")
    {
        color c = parse_color(j.at("color"));
        return make_shared<solid_color>(c);
    }
    else if (type == "checker")
    {
        double scale = j.at("scale").get<double>();
        color even = parse_color(j.at("even"));
        color odd = parse_color(j.at("odd"));
        return make_shared<checker_texture>(scale, even, odd);
    }
    else if (type == "image")
    {
        std::string filename = j.at("filename").get<std::string>();
        return make_shared<image_texture>(filename.c_str());
    }
    else if (type == "noise")
    {
        double scale = j.at("scale").get<double>();
        return make_shared<noise_texture>(scale);
    }
    else
    {
        throw std::runtime_error("Unknown texture type: " + type);
    }
}

inline texture_map load_textures(const json& j)
{
    texture_map textures;

    if (!j.contains("textures"))
    {
        return textures;
    }

    for (auto it = j["textures"].begin(); it != j["textures"].end(); ++it)
    {
        textures[it.key()] = parse_texture(it.value());
    }

    return textures;
}


//------------------------------------------------------------------------------
// Material Loading
//------------------------------------------------------------------------------

inline shared_ptr<material> parse_material(const json& j, const texture_map& textures)
{
    std::string type = j.at("type").get<std::string>();

    if (type == "lambertian")
    {
        // Can have either "albedo" (color) or "texture" (reference)
        if (j.contains("texture"))
        {
            std::string tex_name = j["texture"].get<std::string>();
            auto it = textures.find(tex_name);
            if (it == textures.end())
            {
                throw std::runtime_error("Unknown texture reference: " + tex_name);
            }
            return make_shared<lambertian>(it->second);
        }
        else
        {
            color albedo = parse_color(j.at("albedo"));
            return make_shared<lambertian>(albedo);
        }
    }
    else if (type == "metal")
    {
        color albedo = parse_color(j.at("albedo"));
        double fuzz = get_or(j, "fuzz", 0.0);
        return make_shared<metal>(albedo, fuzz);
    }
    else if (type == "dielectric")
    {
        double ior = j.at("ior").get<double>();
        return make_shared<dielectric>(ior);
    }
    else if (type == "diffuse_light")
    {
        // Can have either "color" or "texture"
        if (j.contains("texture"))
        {
            std::string tex_name = j["texture"].get<std::string>();
            auto it = textures.find(tex_name);
            if (it == textures.end())
            {
                throw std::runtime_error("Unknown texture reference: " + tex_name);
            }
            return make_shared<diffuse_light>(it->second);
        }
        else
        {
            color c = parse_color(j.at("color"));
            return make_shared<diffuse_light>(c);
        }
    }
    else
    {
        throw std::runtime_error("Unknown material type: " + type);
    }
}

inline material_map load_materials(const json& j, const texture_map& textures)
{
    material_map materials;

    if (!j.contains("materials"))
    {
        return materials;
    }

    for (auto it = j["materials"].begin(); it != j["materials"].end(); ++it)
    {
        materials[it.key()] = parse_material(it.value(), textures);
    }

    return materials;
}


//------------------------------------------------------------------------------
// Transform Application
//------------------------------------------------------------------------------

inline shared_ptr<hittable> apply_transforms(shared_ptr<hittable> obj, const json& transforms)
{
    // Transforms are applied in order
    for (const auto& t : transforms)
    {
        if (t.contains("rotate_y"))
        {
            double angle = t["rotate_y"].get<double>();
            obj = make_shared<rotate_y>(obj, angle);
        }
        else if (t.contains("translate"))
        {
            vec3 offset = parse_vec3(t["translate"]);
            obj = make_shared<translate>(obj, offset);
        }
        else
        {
            throw std::runtime_error("Unknown transform type");
        }
    }
    return obj;
}


//------------------------------------------------------------------------------
// Object Loading
//------------------------------------------------------------------------------

// Forward declaration for recursive constant_medium parsing
inline shared_ptr<hittable> parse_object(const json& j, const material_map& materials);

inline shared_ptr<material> get_material(const json& j, const material_map& materials)
{
    if (!j.contains("material"))
    {
        throw std::runtime_error("Object missing 'material' field");
    }

    std::string mat_name = j["material"].get<std::string>();
    auto it = materials.find(mat_name);
    if (it == materials.end())
    {
        throw std::runtime_error("Unknown material reference: " + mat_name);
    }
    return it->second;
}

inline shared_ptr<hittable> parse_object(const json& j, const material_map& materials)
{
    std::string type = j.at("type").get<std::string>();
    shared_ptr<hittable> obj;

    if (type == "sphere")
    {
        point3 center = parse_vec3(j.at("center"));
        double radius = j.at("radius").get<double>();
        auto mat = get_material(j, materials);

        // Check for motion blur (center2)
        if (j.contains("center2"))
        {
            point3 center2 = parse_vec3(j["center2"]);
            obj = make_shared<sphere>(center, center2, radius, mat);
        }
        else
        {
            obj = make_shared<sphere>(center, radius, mat);
        }
    }
    else if (type == "quad")
    {
        point3 Q = parse_vec3(j.at("point"));
        vec3 u = parse_vec3(j.at("u"));
        vec3 v = parse_vec3(j.at("v"));
        auto mat = get_material(j, materials);
        obj = make_shared<quad>(Q, u, v, mat);
    }
    else if (type == "box")
    {
        point3 min_pt = parse_vec3(j.at("min"));
        point3 max_pt = parse_vec3(j.at("max"));
        auto mat = get_material(j, materials);
        obj = box(min_pt, max_pt, mat);
    }
    else if (type == "obj")
    {
        std::string filename = j.at("filename").get<std::string>();
        vec3 scale = get_vec3_or(j, "scale", vec3(1, 1, 1));
        vec3 translate_offset = get_vec3_or(j, "translate", vec3(0, 0, 0));
        double rotate_y_angle = get_or(j, "rotate_y", 0.0);

        // Material is optional for OBJ (can use embedded materials)
        shared_ptr<material> mat = nullptr;
        if (j.contains("material") && !j["material"].is_null())
        {
            std::string mat_name = j["material"].get<std::string>();
            auto it = materials.find(mat_name);
            if (it != materials.end())
            {
                mat = it->second;
            }
        }

        obj = load_obj(filename, scale, translate_offset, rotate_y_angle, mat);
    }
    else if (type == "constant_medium")
    {
        double density = j.at("density").get<double>();
        color albedo = parse_color(j.at("color"));

        // Parse the boundary object recursively
        shared_ptr<hittable> boundary = parse_object(j.at("boundary"), materials);

        obj = make_shared<constant_medium>(boundary, density, albedo);
    }
    else
    {
        throw std::runtime_error("Unknown object type: " + type);
    }

    // Apply transforms if present
    if (j.contains("transform"))
    {
        obj = apply_transforms(obj, j["transform"]);
    }

    return obj;
}

inline hittable_list load_objects(const json& j, const material_map& materials)
{
    hittable_list world;

    if (!j.contains("objects"))
    {
        return world;
    }

    for (const auto& obj_json : j["objects"])
    {
        world.add(parse_object(obj_json, materials));
    }

    return world;
}


//------------------------------------------------------------------------------
// Camera Configuration
//------------------------------------------------------------------------------

inline void configure_camera(camera& cam, const json& j)
{
    if (!j.contains("camera"))
    {
        return;  // Use all defaults
    }

    const json& c = j["camera"];

    cam.aspect_ratio      = get_or(c, "aspect_ratio", 16.0 / 9.0);
    cam.image_width       = get_or(c, "image_width", 400);
    cam.samples_per_pixel = get_or(c, "samples_per_pixel", 100);
    cam.max_depth         = get_or(c, "max_depth", 50);
    cam.background        = get_vec3_or(c, "background", color(0, 0, 0));

    cam.vfov     = get_or(c, "vfov", 90.0);
    cam.lookfrom = get_vec3_or(c, "lookfrom", point3(0, 0, 0));
    cam.lookat   = get_vec3_or(c, "lookat", point3(0, 0, -1));
    cam.vup      = get_vec3_or(c, "vup", vec3(0, 1, 0));

    cam.defocus_angle = get_or(c, "defocus_angle", 0.0);
    cam.focus_dist    = get_or(c, "focus_dist", 10.0);
}


//------------------------------------------------------------------------------
// Main Scene Loading Function
//------------------------------------------------------------------------------

inline scene_data load_scene(const std::string& filename)
{
    // Open and parse the JSON file
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open scene file: " + filename);
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const json::parse_error& e)
    {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }

    // Load textures first (materials may reference them)
    texture_map textures = load_textures(j);

    // Load materials (objects reference them)
    material_map materials = load_materials(j, textures);

    // Load objects into the world
    hittable_list world = load_objects(j, materials);

    // Wrap in BVH for acceleration
    hittable_list bvh_world;
    bvh_world.add(make_shared<bvh_node>(world));

    // Configure camera
    camera cam;
    configure_camera(cam, j);

    return scene_data{cam, bvh_world};
}

#endif
