#ifndef SKWR_IO_SCENE_LOADER_H_
#define SKWR_IO_SCENE_LOADER_H_

//==============================================================================================
// JSON Scene Loader for Skewer Renderer
// Parses JSON scene files and populates a Scene with geometry, materials,
// and returns camera/render configuration.
//==============================================================================================

#include <string>

#include "core/vec3.h"
#include "session/render_options.h"

namespace skwr {

// Forward declarations
class Scene;

// Result of loading a scene file — camera and render parameters
// that the RenderSession uses to configure itself.
struct SceneConfig {
    RenderOptions render_options;

    // Camera parameters
    Vec3 look_from;
    Vec3 look_at;
    Vec3 vup = Vec3(0.0f, 1.0f, 0.0f);
    float vfov = 90.0f;
};

// Load a JSON scene file. Populates the Scene with geometry and materials,
// and returns a SceneConfig with camera and render parameters.
// The Scene's BVH is NOT built — caller must call scene.Build() after.
// Throws std::runtime_error on parse failure.
SceneConfig LoadSceneFile(const std::string& filepath, Scene& scene);

}  // namespace skwr

#endif  // SKWR_IO_SCENE_LOADER_H_
