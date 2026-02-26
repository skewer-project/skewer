#ifndef SKWR_FILM_IMAGE_BUFFER_H_
#define SKWR_FILM_IMAGE_BUFFER_H_

#include <ImfArray.h>

#include <string>
#include <vector>

#include "core/color.h"

namespace skwr {

// Tells compiler "ImageIO" exists
class ImageIO;

class ImageBuffer {
  public:
    ImageBuffer(int width, int height);

    // Set a pixel's color (0,0 is top-left usually)
    void SetPixel(int x, int y, const RGB& color);

    // Save the buffer to a PPM file
    void WritePPM(const std::string& filename) const;

  private:
    int width_;
    int height_;
    std::vector<RGB> pixels_;
};

struct DeepSample {
    // Depth information
    float z_front;
    float z_back;  // for volumes. Can just be z_front for hard surfaces
    float r;
    float g;
    float b;
    float alpha;  // opacity
};

struct DeepPixelView {
    const DeepSample* data;  // Pointer to the actual data
    size_t count;

    // Helper for array-like access
    const DeepSample& operator[](size_t i) const { return data[i]; }
};

class DeepImageBuffer {
    // This gives ImageIO full access to private/protected members
    friend class ImageIO;

  public:
    DeepImageBuffer(int width, int height, size_t totalSamples,
                    const Imf::Array2D<unsigned int>& sampleCounts);

    // Set a pixel's color (0,0 is will be bottom left)
    void SetPixel(int x, int y, const std::vector<DeepSample>& newSamples);

    DeepPixelView GetPixel(int x, int y) const;

    int GetWidth(void) const;
    int GetHeight(void) const;

  private:
    const int width_;
    const int height_;

    std::vector<DeepSample> allSamples_;
    std::vector<size_t> pixelOffsets_;
};

class FlatImageBuffer {
    friend class ImageIO;

  public:
    FlatImageBuffer(int width, int height);

    FlatImageBuffer(int width, int height, std::vector<RGB> pixels);

    // Set a pixel's RGB only (alpha stays at its initialised value of 1.0).
    void SetPixel(int x, int y, const RGB& s);

    // Set a pixel with explicit premultiplied alpha.
    void SetPixel(int x, int y, const RGB& s, float alpha);

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

  private:
    int width_;
    int height_;
    std::vector<RGB> pixels_;
    // Premultiplied alpha channel. Same size as pixels_, initialised to 1.0
    // (fully opaque) so RGB-only writes remain backward-compatible.
    std::vector<float> alpha_;
};

}  // namespace skwr

#endif  // SKWR_FILM_IMAGE_BUFFER_H_
