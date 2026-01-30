#include "session/render_session.h"
#include "core/spectrum.h"
#include "film/film.h"
// #include "integrators/integrator.h"

#include <iostream>
// In Phase 2, we will include the real Integrator and Scene
// #include "integrators/path.h"
// #include "scene/scene.h"

namespace skwr
{

    RenderSession::RenderSession()
    {
        // Initialize pointers to nullptr or default states
        // Pointers default to nullptr implicitly, but explicit is fine
    }

    RenderSession::~RenderSession() = default; // Unique_ptr handles cleanup automatically

    void RenderSession::LoadScene(const std::string &filename)
    {
        std::cout << "[Session] Loading Scene: " << filename << " (STUB)\n";

        // TODO:
        // scene_ = std::make_unique<Scene>();
        // scene_loader::Load(filename, scene_.get());

        // For now, just pretend we loaded something.
        scene_ = std::make_unique<Scene>();
    }

    void RenderSession::SetOptions(int width, int height, int samples_per_pixel) // const RenderOptions &options
    {
        width_ = width;
        height_ = height;
        samples_per_pixel_ = samples_per_pixel;

        // Create the Film
        film_ = std::make_unique<Film>(width_, height_);

        std::cout << "[Session] Options Set: "
                  << width_ << "x" << height_
                  << " | Samples: " << samples_per_pixel_ << "\n";
    }

    void RenderSession::Render()
    {
        if (!film_)
        {
            std::cerr << "[Error] Film not initialized. Call SetOptions first.\n";
            return;
        }

        std::cout << "[Session] Starting Render...\n";

        // --- TEST PATTERN (A "Fake" Integrator) ---
        // Later, this entire loop should move to src/integrators/path.cc
        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {

                // 1. Generate Fake "Data"
                float r = float(x) / (width_ - 1);
                float g = float(y) / (height_ - 1);
                float b = 0.25f;

                // 2. Create a Spectrum (Color)
                Spectrum color(r, g, b);

                // 3. Accumulate to Film
                // Note: We use AddSample, not SetPixel directly!
                film_->AddSample(x, y, color);
            }
        }

        std::cout << "[Session] Render Complete.\n";
    }

    void RenderSession::Save(const std::string &filename) const
    {
        if (film_)
        {
            film_->WriteImage(filename);
        }
    }
} // namespace skwr