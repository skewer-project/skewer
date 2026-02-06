#ifndef SKWR_SESSION_RENDER_OPTIONS_H_
#define SKWR_SESSION_RENDER_OPTIONS_H_

#include <string>

namespace skwr {

enum class IntegratorType {
    PathTrace,
    Normals,
};

struct IntegratorConfig {
    int max_depth;
    int samples_per_pixel;
};

struct ImageConfig {
    int width;
    int height;
    std::string outfile;
};

struct RenderOptions {
    ImageConfig image_config;
    IntegratorConfig integrator_config;
    IntegratorType integrator_type;
};

}  // namespace skwr

#endif  // SKWR_SESSION_RENDER_OPTIONS_H_
