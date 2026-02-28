#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include <string>

#include "film/image_buffer.h"

namespace skwr {

class ImageIO {
  public:
    static void SavePPM(const FlatImageBuffer& buf, const std::string& filename);

    static FlatImageBuffer LoadPPM(const std::string& filename);

    static DeepImageBuffer LoadEXR(const std::string& filename);
};

}  // namespace skwr

#endif
