#include "session/render_session.h"

#include <exrio/deep_image.h>
#include <exrio/deep_writer.h>

#include <cstdint>
#include <iostream>
#include <memory>

#include "core/cpu_config.h"
#include "core/math/vec3.h"
#include "core/spectral/spectral_utils.h"
#include "film/film.h"
#include "film/image_buffer.h"
#include "geometry/boundbox.h"
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

// Derive PNG + EXR output paths from a layer file path and optional output_dir.
// e.g. layer_path="scenes/foo/layer_ball.json", output_dir="images/foo/"
//   → ("images/foo/layer_ball.png", "images/foo/layer_ball.exr")
// output_dir may be a local path or a cloud URI (e.g. "gs://bucket/renders/").
// A trailing separator is added automatically if output_dir doesn't end with one.
static std::pair<std::string, std::string> LayerOutputPaths(const std::string& layer_path,
                                                            const std::string& output_dir) {
    // Strip directory from layer_path to get the bare filename
    size_t slash = layer_path.find_last_of("/\\");
    std::string base = (slash != std::string::npos) ? layer_path.substr(slash + 1) : layer_path;

    // Strip extension
    size_t dot = base.rfind('.');
    std::string stem = (dot != std::string::npos) ? base.substr(0, dot) : base;

    std::string prefix;
    if (!output_dir.empty()) {
        prefix = output_dir;
        char last = prefix.back();
        if (last != '/' && last != '\\') prefix += '/';
    }

    return {prefix + stem + ".png", prefix + stem + ".exr"};
}

void RenderSession::RenderScene(const std::string& scene_file, int thread_override) {
    std::cout << "[Session] Rendering scene: " << scene_file << "\n";

    SceneConfig config = LoadSceneFile(scene_file);

    // Store shared camera params (used by RebuildFilm)
    cam_look_from_ = config.look_from;
    cam_look_at_ = config.look_at;
    cam_vup_ = config.vup;
    cam_vfov_ = config.vfov;
    cam_aperture_ = config.aperture_radius;
    cam_focus_dist_ = config.focus_distance;
    cam_shutter_open_ = config.shutter_open;
    cam_shutter_close_ = config.shutter_close;

    const bool multi_layer = config.layer_paths.size() > 1;

    for (size_t i = 0; i < config.layer_paths.size(); ++i) {
        const std::string& layer_path = config.layer_paths[i];
        std::cout << "[Session] Layer " << (i + 1) << "/" << config.layer_paths.size() << ": "
                  << layer_path << "\n";

        // Fresh scene per layer
        auto layer_scene = std::make_unique<Scene>();
        LoadContextIntoScene(config.context_paths, *layer_scene);
        LayerConfig lcfg = LoadLayerFile(layer_path, *layer_scene);
        layer_scene->SetShutter(cam_shutter_open_, cam_shutter_close_);
        layer_scene->Build();

        auto& opts = lcfg.render_options;
        auto& ic = opts.integrator_config;

        // Multi-layer renders always produce deep EXRs with transparent backgrounds
        if (multi_layer) {
            ic.enable_deep = true;
            ic.transparent_background = true;
        }
        if (thread_override > 0) ic.num_threads = thread_override;

        // Output filenames derived from layer filename + scene-level output_dir
        auto [png_out, exr_out] = LayerOutputPaths(layer_path, config.output_dir);
        opts.image_config.outfile = png_out;
        opts.image_config.exrfile = exr_out;

        float aspect = static_cast<float>(opts.image_config.width) /
                       static_cast<float>(opts.image_config.height);
        auto cam = std::make_unique<Camera>(cam_look_from_, cam_look_at_, cam_vup_, cam_vfov_,
                                            aspect, cam_aperture_, cam_focus_dist_,
                                            cam_shutter_open_, cam_shutter_close_);
        ic.cam_w = -cam->GetW();

        auto film = std::make_unique<Film>(opts.image_config.width, opts.image_config.height);
        auto integ = CreateIntegrator(opts.integrator_type);

        const auto& lic = opts.integrator_config;
        std::cout << "[Session] " << opts.image_config.width << "x" << opts.image_config.height
                  << " | Samples: " << lic.max_samples << " | Depth: " << lic.max_depth << "\n";

        integ->Render(*layer_scene, *cam, film.get(), ic);

        film->WriteImage(opts.image_config.outfile);
        std::cout << "[Session] Wrote " << opts.image_config.outfile << "\n";

        if (ic.enable_deep) {
            exrio::DeepImage img = film->BuildDeepImage();
            exrio::writeDeepEXR(img, opts.image_config.exrfile);
            std::cout << "[Session] Wrote " << opts.image_config.exrfile << "\n";
        }
    }
}

/**
 * Load a scene from a JSON config file.
 * Sets up everything: scene geometry, materials, camera, film, and integrator.
 */
void RenderSession::LoadSceneFromFile(const std::string& scene_file, int thread_override) {
    std::cout << "[Session] Loading scene from: " << scene_file << "\n";

    // 1. Parse scene.json (camera + layer/context paths; no geometry)
    SceneConfig config = LoadSceneFile(scene_file);

    // 2. Create scene, load context + first layer
    scene_ = std::make_unique<Scene>();
    LoadContextIntoScene(config.context_paths, *scene_);

    if (config.layer_paths.empty()) {
        throw std::runtime_error("Scene file has no layers: " + scene_file);
    }
    LayerConfig lcfg = LoadLayerFile(config.layer_paths[0], *scene_);

    scene_->SetShutter(config.shutter_open, config.shutter_close);

    // 3. Build BVH acceleration structure
    scene_->Build();

    // 4. Apply thread override if specified
    if (thread_override > 0) {
        lcfg.render_options.integrator_config.num_threads = thread_override;
    }

    // 5. Store render options
    options_ = lcfg.render_options;

    // 6. Store camera parameters so RebuildFilm() can recreate the camera
    // with the correct aspect ratio if the resolution is later overridden.
    cam_look_from_ = config.look_from;
    cam_look_at_ = config.look_at;
    cam_vup_ = config.vup;
    cam_vfov_ = config.vfov;
    cam_aperture_ = config.aperture_radius;
    cam_focus_dist_ = config.focus_distance;
    cam_shutter_open_ = config.shutter_open;
    cam_shutter_close_ = config.shutter_close;

    // 6. Create camera (aspect ratio derived from image dimensions)
    float aspect = static_cast<float>(options_.image_config.width) /
                   static_cast<float>(options_.image_config.height);
    camera_ = std::make_unique<Camera>(cam_look_from_, cam_look_at_, cam_vup_, cam_vfov_, aspect,
                                       cam_aperture_, cam_focus_dist_, cam_shutter_open_,
                                       cam_shutter_close_);

    // 7. Create film and integrator
    film_ = std::make_unique<Film>(options_.image_config.width, options_.image_config.height);
    integrator_ = CreateIntegrator(options_.integrator_type);
    // GetW() returns the backward-facing basis vector (look_from - look_at).
    // Negate it so cam_w points forward for correct depth projection.
    options_.integrator_config.cam_w = -camera_->GetW();

    const auto& ic = options_.integrator_config;
    std::cout << "[Session] Ready: " << options_.image_config.width << "x"
              << options_.image_config.height << " | Max Samples: " << ic.max_samples
              << " | Max Depth: " << ic.max_depth << "\n";
    if (ic.noise_threshold > 0.0f) {
        std::cout << "[Session] Adaptive: threshold=" << ic.noise_threshold
                  << ", min=" << ic.min_samples << ", step=" << ic.adaptive_step << "\n";
    }
}

void RenderSession::RebuildFilm() {
    // Rebuild camera with the (possibly overridden) aspect ratio so that
    // the projection matches the new film dimensions.
    float aspect = static_cast<float>(options_.image_config.width) /
                   static_cast<float>(options_.image_config.height);
    camera_ = std::make_unique<Camera>(cam_look_from_, cam_look_at_, cam_vup_, cam_vfov_, aspect,
                                       cam_aperture_, cam_focus_dist_, cam_shutter_open_,
                                       cam_shutter_close_);
    options_.integrator_config.cam_w = -camera_->GetW();

    film_ = std::make_unique<Film>(options_.image_config.width, options_.image_config.height);
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
}

/**
 * Convert film to image or deep buffer
 */
void RenderSession::Save() const {
    if (film_) {
        film_->WriteImage(options_.image_config.outfile);

        if (options_.integrator_config.save_sample_map) {
            // Insert "_samples" before the file extension
            std::string out = options_.image_config.outfile;
            auto dot = out.rfind('.');
            std::string map_file = (dot != std::string::npos)
                                       ? out.substr(0, dot) + "_samples" + out.substr(dot)
                                       : out + "_samples.png";
            film_->WriteSampleMap(map_file, options_.integrator_config.max_samples);
        }

        if (options_.integrator_config.enable_deep) {
            exrio::DeepImage img = film_->BuildDeepImage();
            exrio::writeDeepEXR(img, options_.image_config.exrfile);
            std::cout << "Wrote deep image to " << options_.image_config.exrfile << "\n";
        }
    }
}

}  // namespace skwr
