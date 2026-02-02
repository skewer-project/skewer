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
 * Several things are commented out for compilation
 * These should be implemented as development progresses
 */

namespace skwr {

// Forward declarations to avoid circular includes and having to include scene, integrator, etc
class Scene;
class Camera;
class Integrator;
class Film;

class RenderSession {
  public:
    RenderSession();
    ~RenderSession();

    // SETUP: Load data from disk into the Scene object
    void LoadScene(const std::string &filename);

    // CONFIGURE: Set up the camera, resolution, and sampler
    void SetOptions(const RenderOptions &options);

    // EXECUTE: Create the Integrator and tell it to run on the Scene
    void Render();

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
