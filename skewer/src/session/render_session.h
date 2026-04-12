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
    ~RenderSession();

    // Parse scene.json, render every layer, save outputs.
    // Output filenames are derived from each layer filename
    // (e.g. layer_character.json → layer_character.png / .exr).
    void RenderScene(const std::string& scene_file, int thread_override = 0);

    // Override animation timeline from CLI
    void SetAnimationParams(int frame, float fps, float shutter) {
        frame_override_ = frame;
        fps_override_ = fps;
        shutter_override_ = shutter;
        use_animation_overrides_ = true;
    }

    // Update the film buffer if options (like samples_per_pixel) have changed
    void RebuildFilm();

    // --- Legacy API (used by the cloud worker until Phase 5) ---
    void LoadSceneFromFile(const std::string& scene_file, int thread_override = 0);
    void Render();
    void Save() const;
    RenderOptions& Options() { return options_; }
    const RenderOptions& Options() const { return options_; }

  private:
    // The 'World' (Geometry, Lights, Accelerators)
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Camera> camera_;

    // The 'Canvas' (Where pixels end up)
    std::unique_ptr<Film> film_;

    // The 'Worker' (Path Tracer, Volumetric, etc.)
    std::unique_ptr<Integrator> integrator_;

    RenderOptions options_;

    // Camera parameters stored so RebuildFilm() can recreate the camera
    // with a corrected aspect ratio when the resolution is overridden.
    Vec3 cam_look_from_;
    Vec3 cam_look_at_;
    Vec3 cam_vup_;
    float cam_vfov_ = 90.0f;
    float cam_aperture_ = 0.0f;
    float cam_focus_dist_ = 1.0f;
    float cam_shutter_open_ = 0.0f;
    float cam_shutter_close_ = 0.0f;

    bool use_animation_overrides_ = false;
    int frame_override_ = 0;
    float fps_override_ = 24.0f;
    float shutter_override_ = 0.0f;
};

}  // namespace skwr

#endif  // SKWR_SESSION_RENDER_SESSION_H_
