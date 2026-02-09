#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include <cstdint>
#include <string>

#include "film/image_buffer.h"

namespace skwr {

class ImageIO {
  public:
    static void SavePPM(const FlatImageBuffer& buf, const std::string filename);

    static void SaveEXR(const DeepImageBuffer& buf, const std::string filename);

    static void SaveKebab(const DeepImageBuffer& buf, const std::string& filename);

    static FlatImageBuffer LoadPPM(const std::string filename);

    static DeepImageBuffer LoadEXR(const std::string filename);

    static DeepImageBuffer LoadKebab(const std::string& filename);
};

struct KebabHeader {
    char magic[4] = {'K', 'E', 'B', 'B'}; // Unique Magic: KEBB
    uint32_t width;
    uint32_t height;
    uint64_t totalSamples;
};

}  // namespace skwr

#endif
