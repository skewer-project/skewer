#include <iostream>
#include <memory>

#include "core/vec3.h"
#include "film/film.h"
#include "geometry/sphere.h"
#include "integrators/integrator.h"
#include "integrators/normals.h"
#include "integrators/path_trace.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "session/render_options.h"
#include "session/render_session.h"

namespace skwr {

/* FACTORY FUNCTION for Creating Integrators */
static std::unique_ptr<Integrator> CreateIntegrator(IntegratorType type) {
    switch (type) {
        case IntegratorType::PathTrace:
            return std::make_unique<PathTrace>();
        case IntegratorType::Normals:
            return std::make_unique<Normals>();
        default:
            return nullptr;
    }
}

// Initialize pointers to nullptr or default states
// Pointers default to nullptr implicitly, but explicit is fine
RenderSession::RenderSession() {}
RenderSession::~RenderSession() = default;  // Unique_ptr handles cleanup automatically

/**
 * Loads file into scene format
 */
void RenderSession::LoadScene(const std::string &filename) {
    std::cout << "[Session] Loading Scene: " << filename << " (STUB)\n";

    scene_ = std::make_unique<Scene>();

    // Temporarily hardcoding sphere into scene...
    // TODO: Reconnect tinyobjloader and asset loading
    scene_->AddSphere(Sphere{Vec3(0, 0, -3), 1.0f});

    // Initilize camera
    // Looking from (0, 0, 0) to (0, 0, -1)
    // Should these be a part of RenderOptions? Maybe refactor camera first...
    Float aspect = 16.0f / 9.0f;
    camera_ = std::make_unique<Camera>(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 90.0f, aspect);
}

/**
 * Set user options
 */
void RenderSession::SetOptions(const RenderOptions &options) {
    options_ = options;
    film_ = std::make_unique<Film>(options_.image_config.width, options_.image_config.height);
    integrator_ = CreateIntegrator(options_.integrator_type);

    std::cout << "[Session] Options Set: " << options_.image_config.width << "x"
              << options_.image_config.height
              << " | Samples: " << options_.integrator_config.samples_per_pixel << "\n";
}

/**
 * Call integrator render loop
 */
void RenderSession::Render() {
    if (!film_ || !integrator_ || !scene_) {
        std::cerr << "[Error] Session not ready. Missing Film, Integrator, or Scene.\n";
        return;
    }

    std::cout << "[Session] Starting Render...\n";

    integrator_->Render(*scene_, *camera_, film_.get(), options_.integrator_config);
    // .get() extracts the raw pointer held inside. Integrator needs to write pixels to film, so
    // it's mutable, but we don't want to transfer ownership

    std::cout << "[Session] Render Complete.\n";
}

/**
 * Convert film to image or deep buffer
 */
void RenderSession::Save() const {
    if (film_) {
        film_->WriteImage(options_.image_config);
    }
}

}  // namespace skwr
