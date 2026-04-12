#ifndef SKWR_IO_SCENE_LOADER_H_
#define SKWR_IO_SCENE_LOADER_H_

//==============================================================================================
// JSON Scene Loader for Skewer Renderer
// Parses JSON scene files and populates a Scene with geometry, materials,
// and returns camera/render configuration.
//==============================================================================================

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/math/matrix.h"
#include "core/math/quaternion.h"
#include "core/math/vec3.h"
#include "session/render_options.h"

namespace skwr {

// Forward declarations
class Scene;

// Animation Structures
struct KeyframeVec3 {
    float t;
    Vec3 value;
};

struct KeyframeQuat {
    float t;
    Quaternion value;
};

struct KeyframeFloat {
    float t;
    float value;
};

struct AnimationChannelVec3 {
    std::string interpolation;
    std::vector<KeyframeVec3> keyframes;
};

struct AnimationChannelQuat {
    std::string interpolation;
    std::vector<KeyframeQuat> keyframes;
};

struct AnimationChannelFloat {
    std::string interpolation;
    std::vector<KeyframeFloat> keyframes;
};

struct AnimationChannels {
    std::optional<AnimationChannelVec3> translation;
    std::optional<AnimationChannelQuat> rotation;
    std::optional<AnimationChannelFloat> scale;
};

struct SceneNode {
    std::string id;
    std::string name;
    std::string type;
    std::string file;
    std::string material;
    std::string parent;  // ID of parent node or empty if null
    float base_scale = 1.0f;
    AnimationChannels channels;

    // Optimization: pre-calculated matrix for static nodes or base states
    Matrix4 world_matrix;
};

// Per-layer render config extracted from a layer JSON
struct LayerConfig {
    RenderOptions render_options;
    bool visible = true;  // layer-level visibility
};

// Full scene config parsed from scene.json — camera and layer/context file paths.
// Does NOT contain geometry; caller loads layers individually via LoadLayerFile.
struct SceneConfig {
    // Metadata
    std::string name;
    float frame_rate = 24.0f;
    float start_time = 0.0f;
    float end_time = 0.0f;

    // Camera (inline in scene.json, required)
    Vec3 look_from;
    Vec3 look_at;
    Vec3 vup = Vec3(0.0f, 1.0f, 0.0f);
    float vfov = 90.0f;
    float aperture_radius = 0.0f;
    float focus_distance = 1.0f;

    // Context layer paths (resolved to absolute paths)
    std::vector<std::string> context_paths;

    // Render layer paths (resolved to absolute paths, ordered back-to-front)
    std::vector<std::string> layer_paths;

    // Nodes for scene graph
    std::vector<SceneNode> nodes;

    // Optional render settings override from scene file
    std::optional<RenderOptions> render_options;

    // Output directory for all layer renders (local path or cloud URI).
    // Layer stems are appended: "gs://bucket/renders/" + "layer_foo" + ".exr"
    // Empty string means outputs are written to the current working directory.
    std::string output_dir;
};

// Load a scene.json file. Parses camera, context refs, and layer refs.
// Does NOT load any geometry — caller loads layers individually.
// Throws std::runtime_error if "camera" or "layers" is missing.
SceneConfig LoadSceneFile(const std::string& filepath);

// Load a single layer file into a Scene (materials + objects, NO camera).
// Throws std::runtime_error if the file contains a "camera" key.
LayerConfig LoadLayerFile(const std::string& filepath, Scene& scene);

// Merge context layer files into a Scene (adds their materials + objects/lights).
void LoadContextIntoScene(const std::vector<std::string>& context_paths, Scene& scene);

}  // namespace skwr

#endif  // SKWR_IO_SCENE_LOADER_H_
