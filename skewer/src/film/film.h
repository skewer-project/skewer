#ifndef SKWR_FILM_FILM_H_
#define SKWR_FILM_FILM_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/color.h"
#include "film/image_buffer.h"
#include "integrators/path_sample.h"

namespace skwr {

struct Pixel {
    RGB color_sum = RGB(0.0f);       // Accumulated Radiance
    float weight_sum = 0.0f;         // Total weight (filter weight * count)
    std::atomic<int> deep_head{-1};  // Head of linked list
};

struct DeepSegmentNode {
    float z_front;
    float z_back;
    RGB L;
    float alpha;
    int next;
};

class Film {
  public:
    Film(int width, int height);

    void AddSample(int x, int y, const RGB& L, float weight = 1.0f);
    void AddDeepSample(int x, int y, const PathSample& path_sample);

    // Saves to disk (PPM, EXR)
    void WriteImage(const std::string& filename) const;
    std::unique_ptr<DeepImageBuffer> CreateDeepBuffer(const int total_pixel_samples) const;

    int width() { return width_; }
    int height() { return height_; }

  private:
    Pixel& GetPixel(int x, int y) { return pixels_[y * width_ + x]; }
    const Pixel& GetPixel(int x, int y) const { return pixels_[y * width_ + x]; }
    std::vector<DeepSample> MergeDeepSegments(const std::vector<DeepSample>& input,
                                              const int total_pixel_samples) const;

    int width_, height_;
    std::vector<Pixel> pixels_;
    std::vector<DeepSegmentNode> deep_pool_;
    std::atomic<size_t> pool_cursor_{0};
};

}  // namespace skwr

#endif  // SKWR_FILM_FILM_H_
