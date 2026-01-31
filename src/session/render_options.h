#ifndef SKWR_SESSION_RENDER_OPTIONS_H_
#define SKWR_SESSION_RENDER_OPTIONS_H_

#include <string>

namespace skwr {

struct RenderOptions {
    int width;
    int height;
    int samples_per_pixel;
    std::string outfile;
};

}  // namespace skwr

#endif  // SKWR_SESSION_RENDER_OPTIONS_H_
