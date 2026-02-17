#ifndef SKWR_SESSION_RENDER_SESSION_H_
#define SKWR_SESSION_RENDER_SESSION_H_

#include <memory>
#include <string>

#include "core/vec3.h"
#include "film/film.h"
#include "session/render_options.h"

/*
 * The entry point to the engine
 * Orchestrates Scene + Integrator + Film
 *
 * Scenes are loaded from JSON config files via LoadSceneFromFile().
 */

namespace skwr {

// Forward declarations
class Scene;
class Camera;
class Integrator;
class Film;

class RenderSession {
  public:
    RenderSession();
    ~RenderSession();

    // SETUP: Load scene from a JSON config file.
    // Populates scene, camera, film, and integrator from the config.
    // Optional thread_override: if > 0, overrides the thread count from JSON.
    void LoadSceneFromFile(const std::string& scene_file, int thread_override = 0);

    // EXECUTE: Run the integrator on the scene
    void Render();

    // OUTPUT: Write the rendered image to disk
    void Save() const;

  private:
    // The 'World' (Geometry, Lights, Accelerators)
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Camera> camera_;

    // The 'Canvas' (Where pixels end up)
    std::unique_ptr<Film> film_;

    // The 'Worker' (Path Tracer, Volumetric, etc.)
    std::unique_ptr<Integrator> integrator_;

    RenderOptions options_;
};

}  // namespace skwr

#endif  // SKWR_SESSION_RENDER_SESSION_H_
