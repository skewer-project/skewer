#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core/math/vec3.h"
#include "session/render_options.h"
#include "session/render_session.h"

// RunBatchMode renders a single frame as a Cloud Batch task.
//
// Required env vars (set by Cloud Workflows + Cloud Batch):
//   BATCH_TASK_INDEX     — 0-based frame index (set automatically by Cloud Batch)
//   SCENE_URI            — path to scene JSON via GCS FUSE, e.g. /gcs/bucket/scenes/smoke.json
//   OUTPUT_URI_PREFIX    — GCS FUSE output directory, e.g. /gcs/bucket/renders/pipe123/smoke
//
// Optional env vars:
//   CACHE_PREFIX         — if set, copy rendered frame here for content-hash caching
//   PIPELINE_LAYER       — if "true", force enable_deep + transparent_background
static int RunBatchMode() {
    const char* task_index_str = std::getenv("BATCH_TASK_INDEX");
    const char* scene_uri_env = std::getenv("SCENE_URI");
    const char* output_prefix_env = std::getenv("OUTPUT_URI_PREFIX");

    if (!task_index_str || !scene_uri_env || !output_prefix_env) {
        std::cerr << "[SKEWER BATCH]: Missing required env vars (BATCH_TASK_INDEX, SCENE_URI, "
                     "OUTPUT_URI_PREFIX)\n";
        return 1;
    }

    int frame = std::atoi(task_index_str) + 1;  // BATCH_TASK_INDEX is 0-based; frames are 1-based
    char frame_str[8];
    std::snprintf(frame_str, sizeof(frame_str), "%04d", frame);

    // Substitute #### in SCENE_URI with the frame number (for per-frame scene files)
    std::string scene_uri = scene_uri_env;
    {
        const std::string placeholder = "####";
        size_t pos = scene_uri.find(placeholder);
        if (pos != std::string::npos) {
            scene_uri.replace(pos, placeholder.size(), frame_str);
        }
    }

    // Construct output path: OUTPUT_URI_PREFIX/frame-NNNN.exr
    std::string output_prefix = output_prefix_env;
    if (!output_prefix.empty() && output_prefix.back() != '/') output_prefix += '/';
    std::string output_path = output_prefix + "frame-" + frame_str + ".exr";

    std::cout << "[SKEWER BATCH]: Frame " << frame << " | scene: " << scene_uri
              << " | output: " << output_path << "\n";

    try {
        skwr::RenderSession session;

        bool is_pipeline_layer = false;
        if (const char* pl = std::getenv("PIPELINE_LAYER")) {
            is_pipeline_layer = (std::string(pl) == "true");
        }

        if (is_pipeline_layer) {
            // Pipeline mode: SCENE_URI points to a layer file (no camera).
            // Camera params come from env vars set by the workflow.
            auto env_float = [](const char* name, float def) -> float {
                const char* v = std::getenv(name);
                return v ? std::stof(v) : def;
            };
            skwr::Vec3 look_from(env_float("CAM_FROM_X", 0), env_float("CAM_FROM_Y", 0),
                                 env_float("CAM_FROM_Z", 5));
            skwr::Vec3 look_at(env_float("CAM_AT_X", 0), env_float("CAM_AT_Y", 0),
                               env_float("CAM_AT_Z", 0));
            skwr::Vec3 vup(env_float("CAM_VUP_X", 0), env_float("CAM_VUP_Y", 1),
                           env_float("CAM_VUP_Z", 0));
            float vfov = env_float("CAM_VFOV", 90.0f);

            // Parse comma-separated context file paths (lighting, invisible geo)
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
                std::cout << "[SKEWER BATCH]: Loading " << context_paths.size()
                          << " context file(s)\n";
            }

            session.LoadLayerDirect(scene_uri, look_from, look_at, vup, vfov, context_paths);
            session.Options().integrator_config.enable_deep = true;
            session.Options().integrator_config.transparent_background = true;
            std::cout << "[SKEWER BATCH]: Pipeline layer mode: enable_deep + "
                         "transparent_background\n";
        } else {
            session.LoadSceneFromFile(scene_uri);
        }

        session.Options().image_config.outfile = output_path;
        session.Options().image_config.exrfile = output_path;

        session.Render();
        session.Save();
        std::cout << "[SKEWER BATCH]: Render complete: " << output_path << "\n";

        // Copy rendered frame to cache prefix if requested
        if (const char* cache_prefix_env = std::getenv("CACHE_PREFIX")) {
            std::string cache_prefix = cache_prefix_env;
            if (!cache_prefix.empty() && cache_prefix.back() != '/') cache_prefix += '/';
            std::string cache_path = cache_prefix + "frame-" + frame_str + ".exr";
            std::filesystem::create_directories(std::filesystem::path(cache_path).parent_path());
            // Use stream copy instead of filesystem::copy_file because GCS FUSE
            // does not support copy_file_range/sendfile between mount points.
            {
                std::ifstream src(output_path, std::ios::binary);
                std::ofstream dst(cache_path, std::ios::binary);
                if (!src || !dst) {
                    throw std::runtime_error("failed to open files for cache copy: " + output_path +
                                             " -> " + cache_path);
                }
                dst << src.rdbuf();
            }
            std::cout << "[SKEWER BATCH]: Cached to: " << cache_path << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[SKEWER BATCH]: Render failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // Cloud Batch sets BATCH_TASK_INDEX on every task VM.
    if (!std::getenv("BATCH_TASK_INDEX")) {
        std::cerr << "[SKEWER BATCH]: BATCH_TASK_INDEX not set. "
                     "This binary runs as a Cloud Batch task only.\n";
        return 1;
    }

    return RunBatchMode();
}
