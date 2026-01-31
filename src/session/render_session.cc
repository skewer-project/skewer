#include "core/spectrum.h"
#include "film/film.h"
#include "session/render_options.h"
#include "session/render_session.h"
// #include "integrators/integrator.h"

#include <iostream>

// In the next phase, we should include the real Integrator and Scene
// #include "integrators/path.h"
// #include "scene/scene.h"

namespace skwr {

// Initialize pointers to nullptr or default states
// Pointers default to nullptr implicitly, but explicit is fine
RenderSession::RenderSession() {}
RenderSession::~RenderSession() = default;  // Unique_ptr handles cleanup automatically

void RenderSession::LoadScene(const std::string &filename) {
    std::cout << "[Session] Loading Scene: " << filename << " (STUB)\n";

    // TODO: When implementing scene/scene.h
    // scene_ = std::make_unique<Scene>();
    // scene_loader::Load(filename, scene_.get());
}

void RenderSession::SetOptions(const RenderOptions &options) {
    options_ = options;
    // Create the Film
    film_ = std::make_unique<Film>(options_.width, options_.height);

    std::cout << "[Session] Options Set: " << options_.width << "x" << options_.height
              << " | Samples: " << options_.samples_per_pixel << "\n";
}

// TODO: Call a real integrator to begin the rendering process
void RenderSession::Render() {
    if (!film_) {
        std::cerr << "[Error] Film not initialized. Call SetOptions first.\n";
        return;
    }

    std::cout << "[Session] Starting Render...\n";

    // --- TEST PATTERN (A "Fake" Integrator) ---
    // Later, this entire loop should move to src/integrators/path.cc
    for (int y = 0; y < options_.height; ++y) {
        for (int x = 0; x < options_.width; ++x) {
            // Generating fake data
            float r = float(x) / (options_.width - 1);
            float g = float(y) / (options_.height - 1);
            float b = 0.25f;

            // Create a Spectrum (Color)
            Spectrum color(r, g, b);

            // Accumulate to Film
            // Note: We use AddSample, not SetPixel directly!
            film_->AddSample(x, y, color, 1.0f);
        }
    }

    std::cout << "[Session] Render Complete.\n";
}

void RenderSession::Save() const {
    if (film_) {
        film_->WriteImage(options_.outfile);
    }
}

}  // namespace skwr
