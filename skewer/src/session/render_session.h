#ifndef SKWR_SESSION_RENDER_SESSION_H_
#define SKWR_SESSION_RENDER_SESSION_H_

#include <memory>
#include <string>

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
    ~RenderSession() = default;

    // SETUP: Load scene from a JSON config file.
    // Populates scene, camera, film, and integrator from the config.
    // Optional thread_override: if > 0, overrides the thread count from JSON.
    void LoadSceneFromFile(const std::string& scene_file, int thread_override = 0);

    // Update the film buffer if options (like samples_per_pixel) have changed
    static void RebuildFilm();

    // EXECUTE: Run the integrator on the scene
    static void Render();

    // OUTPUT: Write the rendered image to disk
    static void Save() ;

    RenderOptions& Options() { return options_; }
    const RenderOptions& Options() const { return options_; }

  private:
    // The 'World' (Geometry, Lights, Accelerators)
    std::unique_ptr<Scene> scene_{};
    std::unique_ptr<Camera> camera_{};

    // The 'Canvas' (Where pixels end up)
    std::unique_ptr<Film> film_{};

    // The 'Worker' (Path Tracer, Volumetric, etc.)
    std::unique_ptr<Integrator> integrator_{};

    RenderOptions options_;

    // Camera parameters stored so RebuildFilm() can recreate the camera
    // with a corrected aspect ratio when the resolution is overridden.
    Vec3 cam_look_from_;
    Vec3 cam_look_at_;
    Vec3 cam_vup_;
    float cam_vfov_ = 90.0f;
    float cam_aperture_ = 0.0f;
    float cam_focus_dist_ = 1.0f;
};

}  // namespace skwr

#endif  // SKWR_SESSION_RENDER_SESSION_H_
