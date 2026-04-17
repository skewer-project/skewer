#ifndef SKWR_IO_SCENE_LOADER_H_
#define SKWR_IO_SCENE_LOADER_H_

//==============================================================================================
// JSON Scene Loader for Skewer Renderer
// Parses JSON scene files and populates a Scene with geometry, materials,
// and returns camera/render configuration.
//==============================================================================================

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/math/vec3.h"
#include "session/render_options.h"

namespace skwr {

// Forward declarations
class Scene;

// Scene-level animation timeline (optional on scene.json).
struct AnimationConfig {
    float start = 0.0f;
    float end = 0.0f;
    float fps = 24.0f;
    float shutter_angle = 180.0f;

    // end is exclusive: duration = end - start in seconds.
    int NumFrames() const {
        const double duration = static_cast<double>(end) - static_cast<double>(start);
        return static_cast<int>(std::lround(duration * static_cast<double>(fps)));
    }

    // Frame i covers [shutter_open, shutter_close] in scene time (seconds).
    std::pair<float, float> FrameWindow(int frame_idx) const {
        const float t0 = start + static_cast<float>(frame_idx) / fps;
        const float dt = (shutter_angle / 360.0f) / fps;
        return {t0, t0 + dt};
    }
};

// Per-layer render config extracted from a layer JSON
struct LayerConfig {
    RenderOptions render_options;
    bool visible = true;  // layer-level visibility
    bool animated = false;
    bool animated_key_present =
        false;  // false if JSON omits "animated" (warn when scene has animation)
};

// Full scene config parsed from scene.json — camera and layer/context file paths.
// Does NOT contain geometry; caller loads layers individually via LoadLayerFile.
struct SceneConfig {
    // Camera (inline in scene.json, required)
    Vec3 look_from;
    Vec3 look_at;
    Vec3 vup = Vec3(0.0f, 1.0f, 0.0f);
    float vfov = 90.0f;
    float aperture_radius = 0.0f;
    float focus_distance = 1.0f;
    float shutter_open = 0.0f;
    float shutter_close = 0.0f;

    // Context layer paths (resolved to absolute paths)
    std::vector<std::string> context_paths;

    // Render layer paths (resolved to absolute paths, ordered back-to-front)
    std::vector<std::string> layer_paths;

    // Output directory for all layer renders (local path or cloud URI).
    // Layer stems are appended: "gs://bucket/renders/" + "layer_foo" + ".exr"
    // Empty string means outputs are written to the current working directory.
    std::string output_dir;

    // Optional animation timeline; when set, per-layer shutter uses FrameWindow / static time at
    // start.
    std::optional<AnimationConfig> animation;
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
