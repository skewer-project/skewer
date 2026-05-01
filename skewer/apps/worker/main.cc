#include <exrio/deep_image.h>
#include <exrio/deep_writer.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core/spectral/spectral_utils.h"
#include "film/film.h"
#include "integrators/path_trace.h"
#include "io/scene_loader.h"
#include "scene/camera.h"
#include "scene/scene.h"

// RunBatchMode renders one or more frames of a single layer as a Cloud Batch task.
//
// Required env vars (set by Cloud Batch + Cloud Workflows):
//   BATCH_TASK_INDEX     — 0-based task index (set automatically by Cloud Batch)
//   SCENE_URI            — GCS FUSE path to root scene.json (camera + layer refs)
//   LAYER_ID             — layer stem to render (matches layer file basename w/o extension)
//   LAYER_MODE           — "static" or "animated"
//   FRAMES_PER_TASK      — frames per task chunk (animated mode)
//   NUM_FRAMES           — total frames in the animation (animated mode)
//   OUTPUT_URI_PREFIX    — GCS FUSE output directory for this layer
//
// Optional env vars:
//   CACHE_PREFIX         — if set, copy rendered outputs here for content-hash caching
//   CONTEXT_URIS         — comma-separated GCS FUSE paths for context files
static int RunBatchMode() {
    const char* task_index_str = std::getenv("BATCH_TASK_INDEX");
    const char* scene_uri_env = std::getenv("SCENE_URI");
    const char* layer_id_env = std::getenv("LAYER_ID");
    const char* layer_mode_env = std::getenv("LAYER_MODE");
    const char* frames_per_task_env = std::getenv("FRAMES_PER_TASK");
    const char* num_frames_env = std::getenv("NUM_FRAMES");
    const char* output_prefix_env = std::getenv("OUTPUT_URI_PREFIX");

    if (!task_index_str || !scene_uri_env || !layer_id_env || !layer_mode_env ||
        !frames_per_task_env || !num_frames_env || !output_prefix_env) {
        std::cerr << "[SKEWER BATCH]: Missing required env vars (BATCH_TASK_INDEX, SCENE_URI, "
                     "LAYER_ID, LAYER_MODE, FRAMES_PER_TASK, NUM_FRAMES, OUTPUT_URI_PREFIX)\n";
        return 1;
    }

    const int task_index = std::atoi(task_index_str);
    const int frames_per_task = std::atoi(frames_per_task_env);
    const int num_frames = std::atoi(num_frames_env);
    const bool is_animated = (std::string(layer_mode_env) == "animated");

    std::string output_prefix = output_prefix_env;
    if (!output_prefix.empty() && output_prefix.back() != '/') output_prefix += '/';

    // Parse comma-separated context file paths
    std::vector<std::string> context_paths;
    if (const char* ctx_env = std::getenv("CONTEXT_URIS"); ctx_env && ctx_env[0] != '\0') {
        std::string ctx_str(ctx_env);
        size_t start = 0;
        while (start < ctx_str.size()) {
            size_t end = ctx_str.find(',', start);
            if (end == std::string::npos) end = ctx_str.size();
            context_paths.push_back(ctx_str.substr(start, end - start));
            start = end + 1;
        }
    }

    std::cout << "[SKEWER BATCH]: scene=" << scene_uri_env << " layer=" << layer_id_env
              << " mode=" << layer_mode_env << " task=" << task_index << "\n";
    if (!context_paths.empty()) {
        std::cout << "[SKEWER BATCH]: " << context_paths.size() << " context file(s)\n";
    }

    try {
        skwr::InitSpectralModel();

        // Parse the root scene.json once to get camera, layer paths, and animation config.
        skwr::SceneConfig config = skwr::LoadSceneFile(scene_uri_env);

        // Find the target layer path by matching its stem against LAYER_ID.
        std::string layer_path;
        for (const auto& lp : config.layer_paths) {
            auto stem_start = lp.find_last_of("/\\");
            std::string base = (stem_start != std::string::npos) ? lp.substr(stem_start + 1) : lp;
            auto dot = base.rfind('.');
            std::string stem = (dot != std::string::npos) ? base.substr(0, dot) : base;
            if (stem == layer_id_env) {
                layer_path = lp;
                break;
            }
        }
        if (layer_path.empty()) {
            std::cerr << "[SKEWER BATCH]: No layer matching \"" << layer_id_env
                      << "\" found in scene\n";
            return 1;
        }

        // Load the scene (context + layer geometry) once.
        // The BVH is built here and reused across all frames in this chunk.
        auto scene = std::make_unique<skwr::Scene>();
        if (!context_paths.empty()) {
            skwr::LoadContextIntoScene(context_paths, *scene);
        } else if (!config.context_paths.empty()) {
            // Fall back to scene-embedded context paths if none passed via env.
            skwr::LoadContextIntoScene(config.context_paths, *scene);
        }

        skwr::LayerConfig lcfg = skwr::LoadLayerFile(layer_path, *scene);
        skwr::RenderOptions opts = lcfg.render_options;
        // Cloud pipeline layers always render with deep + transparent background
        // so they can be composited cleanly.
        opts.integrator_config.enable_deep = true;
        opts.integrator_config.transparent_background = true;

        const float aspect = static_cast<float>(opts.image_config.width) /
                             static_cast<float>(opts.image_config.height);

        auto render_frame = [&](float t0, float t1, const std::string& out_path) {
            scene->SetShutter(t0, t1);
            scene->Build();  // rebuilds BVH with correct motion bounds for this shutter

            auto cam = std::make_unique<skwr::Camera>(config.look_from, config.look_at, config.vup,
                                                      config.vfov, aspect, config.aperture_radius,
                                                      config.focus_distance, t0, t1);
            opts.integrator_config.cam_w = -cam->GetW();

            auto film =
                std::make_unique<skwr::Film>(opts.image_config.width, opts.image_config.height);
            skwr::PathTrace integ;
            integ.Render(*scene, *cam, film.get(), opts.integrator_config);

            std::filesystem::create_directories(std::filesystem::path(out_path).parent_path());

            film->WriteImage(out_path);
            film->WriteDeepEXRStreaming(out_path);

            std::cout << "[SKEWER BATCH]: Wrote " << out_path << "\n";
        };

        auto copy_to_cache = [&](const std::string& src, const std::string& cache_prefix,
                                 const std::string& filename) {
            std::string dst_dir = cache_prefix;
            if (!dst_dir.empty() && dst_dir.back() != '/') dst_dir += '/';
            std::string dst = dst_dir + filename;
            std::filesystem::create_directories(std::filesystem::path(dst).parent_path());
            std::ifstream in(src, std::ios::binary);
            std::ofstream out(dst, std::ios::binary);
            if (!in || !out) {
                throw std::runtime_error("cache copy failed: " + src + " -> " + dst);
            }
            out << in.rdbuf();
            std::cout << "[SKEWER BATCH]: Cached to: " << dst << "\n";
        };

        if (!is_animated) {
            // Static layer: render once at the animation start time (or scene shutter).
            float t0 = config.shutter_open;
            float t1 = config.shutter_close;
            if (config.animation) {
                t0 = config.animation->start;
                t1 = config.animation->start;
            }

            const std::string out_path = output_prefix + "static.exr";
            render_frame(t0, t1, out_path);

            if (const char* cache_prefix_env = std::getenv("CACHE_PREFIX")) {
                copy_to_cache(out_path, cache_prefix_env, "static.exr");
            }
        } else {
            // Animated layer: render the chunk of frames assigned to this task.
            if (!config.animation) {
                std::cerr
                    << "[SKEWER BATCH]: LAYER_MODE=animated but scene has no animation block\n";
                return 1;
            }

            const int frame_start = task_index * frames_per_task;
            const int frame_end = std::min(frame_start + frames_per_task, num_frames);

            if (frame_start >= num_frames) {
                std::cout << "[SKEWER BATCH]: Task " << task_index
                          << " has no frames to render (frame_start=" << frame_start
                          << " >= num_frames=" << num_frames << ")\n";
                return 0;
            }

            std::cout << "[SKEWER BATCH]: Rendering frames " << frame_start << ".."
                      << (frame_end - 1) << " (task " << task_index << ")\n";

            for (int frame_idx = frame_start; frame_idx < frame_end; ++frame_idx) {
                auto [t0, t1] = config.animation->FrameWindow(frame_idx);

                // 1-based frame number for filename (frame-0001.exr, frame-0002.exr, ...)
                char frame_str[8];
                std::snprintf(frame_str, sizeof(frame_str), "%04d", frame_idx + 1);
                const std::string filename = std::string("frame-") + frame_str + ".exr";
                const std::string out_path = output_prefix + filename;

                render_frame(t0, t1, out_path);

                if (const char* cache_prefix_env = std::getenv("CACHE_PREFIX")) {
                    copy_to_cache(out_path, cache_prefix_env, filename);
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[SKEWER BATCH]: Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!std::getenv("BATCH_TASK_INDEX")) {
        std::cerr << "[SKEWER BATCH]: BATCH_TASK_INDEX not set. "
                     "This binary runs as a Cloud Batch task only.\n";
        return 1;
    }

    return RunBatchMode();
}
