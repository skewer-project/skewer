#ifndef SKWR_FILM_FILM_H_
#define SKWR_FILM_FILM_H_

#include <exrio/deep_image.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/color/color.h"
#include "core/transport/path_sample.h"
#include "film/deep_segment_pool.h"
#include "film/image_buffer.h"

namespace skwr {

struct Pixel {
    RGB color_sum = RGB(0.0f);       // Accumulated Radiance (premultiplied by alpha)
    float alpha_sum = 0.0f;          // Accumulated coverage
    float weight_sum = 0.0f;         // Total weight (filter weight * count)
    std::atomic<int> deep_head{-1};  // Head of linked list
};

class Film {
  public:
    Film(int width, int height);

    // alpha is the flat-pass coverage for this sample (0=transparent, 1=opaque).
    void AddSample(int x, int y, const RGB& L, float alpha, float weight = 1.0f);
    void AddDeepSample(int x, int y, const PathSample& path_sample);

    // Saves to disk (PNG, EXR)
    void WriteImage(const std::string& filename) const;
    exrio::DeepImage BuildDeepImage(const int total_pixel_samples) const;

    // Builds a flat RGBA buffer suitable for export as a compositing-friendly EXR.
    // Colors are premultiplied; alpha reflects average coverage per pixel.
    std::unique_ptr<FlatImageBuffer> CreateFlatBuffer() const;

    int width() { return width_; }
    int height() { return height_; }

  private:
    Pixel& GetPixel(int x, int y) { return pixels_[y * width_ + x]; }
    const Pixel& GetPixel(int x, int y) const { return pixels_[y * width_ + x]; }
    std::vector<DeepSample> MergeDeepSegments(const std::vector<DeepSample>& input,
                                              const int total_pixel_samples) const;

    int width_, height_;
    std::vector<Pixel> pixels_;
    DeepSegmentPool deep_pool_;
};

}  // namespace skwr

#endif  // SKWR_FILM_FILM_H_
