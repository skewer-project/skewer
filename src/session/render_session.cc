#include "session/render_session.h"

#include <iostream>
#include <memory>

#include "core/spectral/spectral_utils.h"
#include "core/vec3.h"
#include "film/film.h"
#include "film/image_buffer.h"
#include "integrators/integrator.h"
#include "integrators/normals.h"
#include "integrators/path_trace.h"
#include "io/image_io.h"
#include "io/scene_loader.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "session/render_options.h"

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

RenderSession::RenderSession() { skwr::InitSpectralModel(); }
RenderSession::~RenderSession() = default;

/**
 * Load a scene from a JSON config file.
 * Sets up everything: scene geometry, materials, camera, film, and integrator.
 */
void RenderSession::LoadSceneFromFile(const std::string& scene_file, int thread_override) {
    std::cout << "[Session] Loading scene from: " << scene_file << "\n";

    // 1. Create scene and load from JSON
    scene_ = std::make_unique<Scene>();
    SceneConfig config = LoadSceneFile(scene_file, *scene_);

    // 2. Build BVH acceleration structure
    scene_->Build();

    // 3. Apply thread override if specified
    if (thread_override > 0) {
        config.render_options.integrator_config.num_threads = thread_override;
    }

    // 4. Store render options
    options_ = config.render_options;

    // 5. Create camera (aspect ratio derived from image dimensions)
    float aspect = static_cast<float>(options_.image_config.width) /
                   static_cast<float>(options_.image_config.height);
    camera_ =
        std::make_unique<Camera>(config.look_from, config.look_at, config.vup, config.vfov, aspect);

    // 6. Create film and integrator
    film_ = std::make_unique<Film>(options_.image_config.width, options_.image_config.height);
    integrator_ = CreateIntegrator(options_.integrator_type);
    options_.integrator_config.cam_w = camera_->GetW();

    std::cout << "[Session] Ready: " << options_.image_config.width << "x"
              << options_.image_config.height
              << " | Samples: " << options_.integrator_config.samples_per_pixel
              << " | Max Depth: " << options_.integrator_config.max_depth << "\n";
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

    std::cout << "[Session] Render Complete.\n";
}

/**
 * Convert film to image or deep buffer
 */
void RenderSession::Save() const {
    if (film_) {
        film_->WriteImage(options_.image_config.outfile);
        if (options_.integrator_config.enable_deep) {
            std::unique_ptr<DeepImageBuffer> buf =
                film_->CreateDeepBuffer(options_.integrator_config.samples_per_pixel);
            ImageIO::SaveEXR(*buf, options_.image_config.exrfile);
        }
    }
}

}  // namespace skwr
