#include "session/render_session.h"

#include <iostream>
#include <memory>

#include "film/film.h"
#include "integrators/integrator.h"
#include "integrators/path_integrator.h"
#include "session/render_options.h"

namespace skwr {

// Initialize pointers to nullptr or default states
// Pointers default to nullptr implicitly, but explicit is fine
RenderSession::RenderSession() {}
RenderSession::~RenderSession() = default;  // Unique_ptr handles cleanup automatically

void RenderSession::LoadScene(const std::string& filename) {
    std::cout << "[Session] Loading Scene: " << filename << " (STUB)\n";

    // TODO: When implementing scene/scene.h
    scene_ = std::make_unique<Scene>();
    // scene_loader::Load(filename, scene_.get());
}

void RenderSession::SetOptions(const RenderOptions& options) {
    options_ = options;
    // Create the Film
    film_ = std::make_unique<Film>(options_.width, options_.height);
    integrator_ = std::make_unique<PathIntegrator>();

    std::cout << "[Session] Options Set: " << options_.width << "x" << options_.height
              << " | Samples: " << options_.samples_per_pixel << "\n";
}

void RenderSession::Render() {
    if (!film_ || !integrator_ || !scene_) {
        std::cerr << "[Error] Session not ready. Missing Film, Integrator, or Scene.\n";
        return;
    }

    std::cout << "[Session] Starting Render...\n";

    integrator_->Render(*scene_, film_.get());
    // .get() extracts the raw pointer held inside. Integrator needs to write pixels to film, so
    // it's mutable, but we don't want to transfer ownership

    std::cout << "[Session] Render Complete.\n";
}

void RenderSession::Save() const {
    if (film_) {
        film_->WriteImage(options_.outfile);
    }
}

}  // namespace skwr
