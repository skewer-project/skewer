#include "session/render_session.h"

#include <cstdint>
#include <iostream>
#include <memory>

#include "core/math/vec3.h"
#include "core/spectral/spectral_utils.h"
#include "film/film.h"
#include "film/image_buffer.h"
#include "integrators/integrator.h"
#include "integrators/normals.h"
#include "integrators/path_trace.h"
#include "io/image_io.h"
#include "io/scene_loader.h"
#include "materials/material.h"
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

    // TEMPORARY HARDCODED GLOBAL FOG
    HomogeneousMedium fog;
    // Low density so we can still see the scene! (e.g., 0.05)
    // Spectrum is RGB or whatever your spectral layout is
    fog.sigma_a = Spectrum(0.05f);
    fog.sigma_s = Spectrum(0.01f);
    fog.g = 0.6f;  // isotropic

    // 3. Register it and set it as global
    uint16_t fog_id = scene_->AddHomogeneousMedium(fog);
    // scene_->SetGlobalMedium(fog_id);
    // {
    //   "type": "sphere",
    //   "material": "glass",
    //   "center": [0.0, -3.5, -10.0],
    //   "radius": 1.5
    // }
    Material mat{};
    mat.type = MaterialType::Dielectric;
    mat.albedo = RGBToCurve(RGB(1.0f));
    mat.ior = 1.5f;
    mat.roughness = 0.0f;
    mat.dispersion = 0.2f;
    uint32_t gl = scene_->AddMaterial(mat);
    scene_->AddSphere(Sphere{Point3(0.0f, -3.5f, -10.0f), 1.5f, gl, fog_id, 0, 2});

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
