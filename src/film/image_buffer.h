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
    float z_front;
    float z_back; // for volumes. Can just be z_front for hard surfaces
    Spectrum color;
    float transmittance;
};

struct DeepPixel {
    std::vector<DeepSample> samples;

    void Sort(); // OpenEXR expects samples to be sorted by depth
};

class DeepImageBuffer {
    public:
      DeepImageBuffer(int width, int height);

      // Set a pixel's color (0,0 is top-left usually)
      void SetPixel(int x, int y, const DeepPixel &p);

    private:
      int width_;
      int height_;
      std::vector<DeepPixel> pixels_; // Contiguous memory but allows [x][y] indexing

      // NOTE: might have to change from array of structures (DeepPixel) to structure of arrays
      // std::vector<half> z_;
      // std::vector<half> r_;
      // std::vector<half> g_;
      // std::vector<half> b_;
      // std::vector<half> a_;

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
