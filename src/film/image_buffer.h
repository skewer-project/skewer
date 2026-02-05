#ifndef SKWR_FILM_IMAGE_BUFFER_H_
#define SKWR_FILM_IMAGE_BUFFER_H_

#include <ImfArray.h>

#include <string>
#include <vector>

#include "core/spectrum.h"

namespace skwr {

class ImageBuffer {
  public:
    ImageBuffer(int width, int height);

    // Set a pixel's color (0,0 is top-left usually)
    void SetPixel(int x, int y, const Spectrum& s);

    // Save the buffer to a PPM file
    void WritePPM(const std::string& filename) const;

  private:
    int width_;
    int height_;
    std::vector<Spectrum> pixels_;
};

struct DeepSample {
    // Depth information
    Float z_front;
    Float z_back;  // for volumes. Can just be z_front for hard surfaces
    Spectrum color;
    Float alpha;  // opacity

    // For sorting later on
    bool operator<(const DeepSample& other) const {
        if (z_front != other.z_front) return z_front < other.z_front;
        return z_back < other.z_back;
    }
};

// Necessary for Film interface
struct DeepPixel {
    std::vector<DeepSample> samples;
};

struct DeepPixelView {
    const DeepSample* data;  // Pointer to the actual data
    size_t count;

    // Helper for array-like access
    const DeepSample& operator[](size_t i) const { return data[i]; }
};

struct MutableDeepPixelView {
    DeepSample* data;  // Mutable pointer
    size_t count;

    // Helper for array-like access
    DeepSample& operator[](size_t i) { return data[i]; }
};

class DeepImageBuffer {
  public:
    DeepImageBuffer(int width, int height, size_t totalSamples,
                    const Imf::Array2D<unsigned int>& sampleCounts);

    // Set a pixel's color (0,0 is will be bottom left)
    void SetPixel(int x, int y, const std::vector<DeepSample>& newSamples);

    DeepPixelView GetPixel(int x, int y) const;
    MutableDeepPixelView GetMutablePixel(int x, int y);

    int GetWidth(void) const;
    int GetHeight(void) const;

  private:
    const int width_;
    const int height_;

    std::vector<DeepSample> allSamples_;
    std::vector<size_t> pixelOffsets_;
};

class FlatImageBuffer {
  public:
    FlatImageBuffer(int width, int height) : width_(width), height_(height), pixels_({}) {};

    FlatImageBuffer(int width, int height, std::vector<Spectrum> pixels)
        : width_(width), height_(height), pixels_(pixels) {};

    // Set a pixel's color (0,0 is top-left usually)
    void SetPixel(int x, int y, const Spectrum& s);

  private:
    int width_;
    int height_;
    std::vector<Spectrum> pixels_;
};

}  // namespace skwr

#endif  // SKWR_FILM_IMAGE_BUFFER_H_
