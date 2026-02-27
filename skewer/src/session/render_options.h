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
    int start_sample;
    int num_threads = 0;  // 0 = auto-detect (hardware_concurrency)
    bool enable_deep = false;
    // When true, primary rays that miss all geometry produce alpha=0 instead of
    // opaque black. Enables clean layer compositing without a black background matte.
    bool transparent_background = false;

    // How many surface bounces are checked when deciding if a pixel is "covered"
    // by a visible object. Only meaningful when transparent_background=true.
    //   1 (default) – only the first surface hit must be visible (strict camera visibility)
    //   2-4         – allows seeing visible objects through N-1 invisible surfaces
    //                 e.g. a visible sphere seen in an invisible mirror
    int visibility_depth = 1;

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
