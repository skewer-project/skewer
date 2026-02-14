#ifndef SKWR_SESSION_RENDER_OPTIONS_H_
#define SKWR_SESSION_RENDER_OPTIONS_H_

#include <string>

#include "core/vec3.h"

namespace skwr {

enum class IntegratorType {
    PathTrace,
    Normals,
};

struct IntegratorConfig {
    int max_depth;
    int samples_per_pixel;
    int num_threads = 0;  // 0 = auto-detect (hardware_concurrency)
    bool enable_deep = false;
    Vec3 cam_w;
};

struct ImageConfig {
    int width;
    int height;
    std::string outfile;
    std::string exrfile;
};

struct RenderOptions {
    ImageConfig image_config;
    IntegratorConfig integrator_config;
    IntegratorType integrator_type;
};

}  // namespace skwr

#endif  // SKWR_SESSION_RENDER_OPTIONS_H_
