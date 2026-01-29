#ifndef SKWR_SESSION_RENDER_SESSION_H_
#define SKWR_SESSION_RENDER_SESSION_H_

#include <memory>
#include <string>

/*
 * The entry point to the engine
 * Orchestrates Scene + Integrator + Film
 */

// Forward declarations to avoid circular includes
namespace skwr
{
    class Scene;
    class Integrator;
    class Film;
}

namespace skwr
{

    class RenderSession
    {
    public:
        RenderSession();
        ~RenderSession();

        // 1. SETUP: Loads data from disk into the Scene object
        void LoadScene(const std::string &filename);

        // 2. CONFIGURE: Sets up the camera, resolution, and sampler
        void SetOptions(int width, int height, int samples_per_pixel);

        // 3. EXECUTE: Creates the Integrator and tells it to run on the Scene
        void Render();

    private:
        // The 'World' (Geometry, Lights, Accelerators)
        std::unique_ptr<Scene> scene_;

        // The 'Canvas' (Where pixels end up)
        std::unique_ptr<Film> film_;

        // The 'Worker' (Path Tracer, Volumetric, etc.)
        std::unique_ptr<Integrator> integrator_;
    };

} // namespace skwr

#endif // SKWR_SESSION_RENDER_SESSION_H_