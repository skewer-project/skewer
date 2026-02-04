#ifndef DEEP_IMAGE_H
#define DEEP_IMAGE_H

#include <string>

#include "film/image_buffer.h"

namespace skwr {

class ImageIO {
  public:
    static void SavePPM(const FlatImageBuffer& film);

    static void SaveEXR(const DeepImageBuffer& film);

    static FlatImageBuffer LoadPPM(const std::string filename);

    static DeepImageBuffer LoadEXR(const std::string filename);
};

struct ChannelInfo {
    const char* name;
    bool required;
    bool present;
};

}  // namespace skwr

#endif
