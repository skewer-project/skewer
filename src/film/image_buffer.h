#ifndef SKWR_FILM_IMAGE_BUFFER_H_
#define SKWR_FILM_IMAGE_BUFFER_H_

#include <string>
#include <vector>

#include "core/spectrum.h"

namespace skwr {

class ImageBuffer {
  public:
    ImageBuffer(int width, int height);

    // Set a pixel's color (0,0 is top-left usually)
    void SetPixel(int x, int y, const Spectrum &s);

    // Save the buffer to a PPM file
    void WritePPM(const std::string &filename) const;

  private:
    int width_;
    int height_;
    std::vector<Spectrum> pixels_;
};

}  // namespace skwr

#endif  // SKWR_FILM_IMAGE_BUFFER_H_
