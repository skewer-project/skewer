#include "session/render_session.h"

#include <exrio/deep_image.h>
#include <exrio/deep_writer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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

// Insert ".NNNN" immediately before the final extension (e.g. beauty.png → beauty.0042.png).
static std::string InsertFrameBeforeExtension(const std::string& path, int frame_idx) {
    char frame_buf[16];
    std::snprintf(frame_buf, sizeof(frame_buf), ".%04d", frame_idx);

    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) {
        return path + frame_buf;
    }
    return path.substr(0, dot) + frame_buf + path.substr(dot);
}

static std::pair<std::string, std::string> LayerOutputPathsWithFrame(const std::string& layer_path,
                                                                     const std::string& output_dir,
                                                                     int frame_idx) {
    auto [png, exr] = LayerOutputPaths(layer_path, output_dir);
    return {InsertFrameBeforeExtension(png, frame_idx), InsertFrameBeforeExtension(exr, frame_idx)};
}

static std::string LayerStemFromPath(const std::string& layer_path) {
    size_t slash = layer_path.find_last_of("/\\");
    std::string base = (slash != std::string::npos) ? layer_path.substr(slash + 1) : layer_path;
    size_t dot = base.rfind('.');
    return (dot != std::string::npos) ? base.substr(0, dot) : base;
}

static bool LayerStemMatches(const std::string& layer_path, const std::string& stem_or_path) {
    if (layer_path == stem_or_path) return true;
    return LayerStemFromPath(layer_path) == stem_or_path;
}

static void RenderLayerPass(const SceneConfig& config, const std::string& layer_path,
                            float shutter_open, float shutter_close,
                            const std::pair<std::string, std::string>& out_paths,
                            int thread_override, bool multi_layer) {
    auto layer_scene = std::make_unique<Scene>();
    LoadContextIntoScene(config.context_paths, *layer_scene);
    LayerConfig lcfg = LoadLayerFile(layer_path, *layer_scene);
    layer_scene->SetShutter(shutter_open, shutter_close);
    layer_scene->Build();

    RenderOptions opts = lcfg.render_options;
    auto& ic = opts.integrator_config;

    if (multi_layer) {
        ic.enable_deep = true;
        ic.transparent_background = true;
    }
    if (thread_override > 0) ic.num_threads = thread_override;

    opts.image_config.outfile = out_paths.first;
    opts.image_config.exrfile = out_paths.second;

    float aspect =
        static_cast<float>(opts.image_config.width) / static_cast<float>(opts.image_config.height);
    auto cam = std::make_unique<Camera>(config.look_from, config.look_at, config.vup, config.vfov,
                                        aspect, config.aperture_radius, config.focus_distance,
                                        shutter_open, shutter_close);
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
        // Capture stats before the streaming writer clears per-row buckets.
        DeepBucketStats ds = film->GetDeepBucketStats();
        std::cout << "[Session] Deep stats: pixels_with_buckets=" << ds.pixels_with_buckets
                  << " total_buckets=" << ds.total_buckets
                  << " peak/pixel=" << ds.peak_buckets_per_pixel
                  << " forced_evictions=" << ds.forced_evictions << "\n";
        film->WriteDeepEXRStreaming(opts.image_config.exrfile);
        std::cout << "[Session] Wrote " << opts.image_config.exrfile << "\n";
    }
}

void RenderSession::RenderScene(const std::string& scene_file, int thread_override) {
    RenderScene(scene_file, RenderCliOptions{}, thread_override);
}

void RenderSession::RenderScene(const std::string& scene_file, const RenderCliOptions& cli,
                                int thread_override) {
    std::cout << "[Session] Rendering scene: " << scene_file << "\n";

    SceneConfig config = LoadSceneFile(scene_file);

    cam_look_from_ = config.look_from;
    cam_look_at_ = config.look_at;
    cam_vup_ = config.vup;
    cam_vfov_ = config.vfov;
    cam_aperture_ = config.aperture_radius;
    cam_focus_dist_ = config.focus_distance;
    if (config.animation) {
        cam_shutter_open_ = config.animation->start;
        cam_shutter_close_ = config.animation->start;
    } else {
        cam_shutter_open_ = config.shutter_open;
        cam_shutter_close_ = config.shutter_close;
    }

    if (cli.statics_only && cli.only_listed_frames) {
        throw std::runtime_error("--statics-only cannot be combined with --frame or --frames");
    }
    if (cli.only_listed_frames && !config.animation) {
        throw std::runtime_error("--frame / --frames require an \"animation\" block in the scene");
    }

    const int num_anim_frames = config.animation ? config.animation->NumFrames() : 0;

    if (cli.only_listed_frames) {
        for (int idx : cli.frame_indices) {
            if (idx < 0 || idx >= num_anim_frames) {
                throw std::runtime_error("Frame index " + std::to_string(idx) +
                                         " out of range [0, " + std::to_string(num_anim_frames) +
                                         ")");
            }
        }
    }

    const bool multi_layer = config.layer_paths.size() > 1;
    const bool frame_mode = cli.only_listed_frames && !cli.statics_only;

    for (size_t i = 0; i < config.layer_paths.size(); ++i) {
        const std::string& layer_path = config.layer_paths[i];
        std::cout << "[Session] Layer " << (i + 1) << "/" << config.layer_paths.size() << ": "
                  << layer_path << "\n";

        LayerAnimationFlags anim_flags = PeekLayerAnimationFlags(layer_path);

        if (anim_flags.animated && !config.animation) {
            throw std::runtime_error(
                "Layer has \"animated\": true but scene has no \"animation\" block: " + layer_path);
        }

        if (config.animation && !anim_flags.animated_key_present) {
            std::cerr << "[Warning] Layer \"" << layer_path
                      << "\" omits \"animated\"; defaulting to false.\n";
        }

        if (anim_flags.animated) {
            if (cli.statics_only) {
                continue;
            }
            const AnimationConfig& anim = *config.animation;
            std::vector<int> frames;
            if (cli.only_listed_frames) {
                frames = cli.frame_indices;
            } else {
                frames.reserve(static_cast<size_t>(num_anim_frames));
                for (int f = 0; f < num_anim_frames; ++f) {
                    frames.push_back(f);
                }
            }

            for (int frame_idx : frames) {
                auto [open_s, close_s] = anim.FrameWindow(frame_idx);
                auto outs = LayerOutputPathsWithFrame(layer_path, config.output_dir, frame_idx);
                std::cout << "[Session] Frame " << frame_idx << " (shutter " << open_s << " — "
                          << close_s << ")\n";
                RenderLayerPass(config, layer_path, open_s, close_s, outs, thread_override,
                                multi_layer);
            }
        } else {
            if (frame_mode) {
                continue;
            }
            float open_s = config.shutter_open;
            float close_s = config.shutter_close;
            if (config.animation) {
                open_s = config.animation->start;
                close_s = config.animation->start;
            }
            auto outs = LayerOutputPaths(layer_path, config.output_dir);
            RenderLayerPass(config, layer_path, open_s, close_s, outs, thread_override,
                            multi_layer);
        }
    }
}

void RenderSession::RenderFrame(const std::string& scene_file, const std::string& layer_stem,
                                int frame_idx, int thread_override) {
    SceneConfig config = LoadSceneFile(scene_file);
    if (!config.animation) {
        throw std::runtime_error("RenderFrame requires a scene with an \"animation\" block");
    }

    const int num_anim_frames = config.animation->NumFrames();
    if (frame_idx < 0 || frame_idx >= num_anim_frames) {
        throw std::runtime_error("Frame index " + std::to_string(frame_idx) + " out of range [0, " +
                                 std::to_string(num_anim_frames) + ")");
    }

    std::string matched;
    for (const auto& p : config.layer_paths) {
        if (LayerStemMatches(p, layer_stem)) {
            matched = p;
            break;
        }
    }
    if (matched.empty()) {
        throw std::runtime_error("No layer matching \"" + layer_stem + "\" in scene");
    }

    LayerAnimationFlags anim_flags = PeekLayerAnimationFlags(matched);
    if (!anim_flags.animated) {
        throw std::runtime_error("RenderFrame target layer is not marked animated: " + matched);
    }

    const bool multi_layer = config.layer_paths.size() > 1;
    auto [open_s, close_s] = config.animation->FrameWindow(frame_idx);
    auto outs = LayerOutputPathsWithFrame(matched, config.output_dir, frame_idx);

    std::cout << "[Session] RenderFrame: " << matched << " @ frame " << frame_idx << " (shutter "
              << open_s << " — " << close_s << ")\n";

    RenderLayerPass(config, matched, open_s, close_s, outs, thread_override, multi_layer);
}

void RenderSession::LoadLayerDirect(const std::string& layer_file, Vec3 look_from, Vec3 look_at,
                                    Vec3 vup, float vfov,
                                    const std::vector<std::string>& context_paths) {
    std::cout << "[Session] Loading layer directly: " << layer_file << "\n";

    scene_ = std::make_unique<Scene>();
    if (!context_paths.empty()) {
        std::cout << "[Session] Loading " << context_paths.size() << " context file(s)\n";
        LoadContextIntoScene(context_paths, *scene_);
    }
    LayerConfig lcfg = LoadLayerFile(layer_file, *scene_);
    scene_->Build();

    options_ = lcfg.render_options;

    cam_look_from_ = look_from;
    cam_look_at_ = look_at;
    cam_vup_ = vup;
    cam_vfov_ = vfov;

    float aspect = static_cast<float>(options_.image_config.width) /
                   static_cast<float>(options_.image_config.height);
    camera_ = std::make_unique<Camera>(cam_look_from_, cam_look_at_, cam_vup_, cam_vfov_, aspect,
                                       cam_aperture_, cam_focus_dist_);

    film_ = std::make_unique<Film>(options_.image_config.width, options_.image_config.height);
    integrator_ = CreateIntegrator(options_.integrator_type);
    options_.integrator_config.cam_w = -camera_->GetW();

    const auto& ic = options_.integrator_config;
    std::cout << "[Session] Ready: " << options_.image_config.width << "x"
              << options_.image_config.height << " | Max Samples: " << ic.max_samples
              << " | Max Depth: " << ic.max_depth << "\n";
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
            DeepBucketStats ds = film_->GetDeepBucketStats();
            std::cout << "[Session] Deep stats: pixels_with_buckets=" << ds.pixels_with_buckets
                      << " total_buckets=" << ds.total_buckets
                      << " peak/pixel=" << ds.peak_buckets_per_pixel
                      << " forced_evictions=" << ds.forced_evictions << "\n";
            film_->WriteDeepEXRStreaming(options_.image_config.exrfile);
            std::cout << "Wrote deep image to " << options_.image_config.exrfile << "\n";
        }
    }
}

}  // namespace skwr
