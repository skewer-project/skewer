#ifndef SKWR_FILM_IMAGE_BUFFER_H_
#define SKWR_FILM_IMAGE_BUFFER_H_

#include <string>
#include <vector>
#include <ImfArray.h>

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

struct DeepSample {
    // Depth information
    Float z_front;
    Float z_back; // for volumes. Can just be z_front for hard surfaces
    Spectrum color;
    Float alpha; // opacity

    // For sorting later on
    bool operator<(const DeepSample& other) const {
        if (z_front != other.z_front)
            return z_front < other.z_front;
        return z_back < other.z_back;
    }
};

struct DeepPixel {
    std::vector<DeepSample> samples;

    void Sort(); // OpenEXR expects samples to be sorted by depth
};

class DeepImageBuffer {
    public:
      DeepImageBuffer(int width, int height)
        : width_(width), height_(height), pixels_(width * height) {};

      // Set a pixel's color (0,0 is will be bottom left)
      void SetPixel(int x, int y, const DeepPixel &p);

      DeepPixel& GetPixel(int x, int y);

    private:
      int width_;
      int height_;
      std::vector<DeepPixel> pixels_;
};

class FlatImageBuffer {
    public:
      FlatImageBuffer(int width, int height) : width_(width), height_(height), pixels_({}) {};

      FlatImageBuffer(int width, int height, std::vector<Spectrum> pixels)
        : width_(width), height_(height), pixels_(pixels) {};

      // Set a pixel's color (0,0 is top-left usually)
      void SetPixel(int x, int y, const Spectrum &s);

    private:
      int width_;
      int height_;
      std::vector<Spectrum> pixels_;
};

}  // namespace skwr

#endif  // SKWR_FILM_IMAGE_BUFFER_H_
