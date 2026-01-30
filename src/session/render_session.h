#ifndef SKWR_SESSION_RENDER_SESSION_H_
#define SKWR_SESSION_RENDER_SESSION_H_

#include <memory>
#include <string>

/*
 * The entry point to the engine
 * Orchestrates Scene + Integrator + Film
 */

// Forward declarations to avoid circular includes and having to include scene, integrator, etc
namespace skwr
{
    class Scene;
    class Integrator;
    class Film;

    // struct RenderOptions
    // {
    //     int width_;
    //     int height_;
    //     int samples_per_pixel_;
    //     integrator;
    // };

    class RenderSession
    {
    public:
        RenderSession();
        ~RenderSession();

        // SETUP: Load data from disk into the Scene object
        void LoadScene(const std::string &filename);

        // CONFIGURE: Set up the camera, resolution, and sampler
        // TODO: later, implement a dedicated options class: SetOptions(const RenderOptions &options)
        void SetOptions(int width, int height, int samples_per_pixel);

        // EXECUTE: Create the Integrator and tell it to run on the Scene
        void Render();

        void Save(const std::string &filename) const;

    private:
        // The 'World' (Geometry, Lights, Accelerators)
        std::unique_ptr<Scene> scene_;

        // The 'Canvas' (Where pixels end up)
        std::unique_ptr<Film> film_;

        // The 'Worker' (Path Tracer, Volumetric, etc.)
        std::unique_ptr<Integrator> integrator_;

        // For now just storing directly. Later, store a RenderOptions struct...
        int width_, height_, samples_per_pixel_;
    };

} // namespace skwr

#endif // SKWR_SESSION_RENDER_SESSION_H_