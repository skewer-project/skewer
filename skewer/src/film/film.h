#ifndef SKWR_FILM_FILM_H_
#define SKWR_FILM_FILM_H_

#include <exrio/deep_image.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/color.h"
#include "film/image_buffer.h"
#include "integrators/path_sample.h"

namespace skwr {

struct Pixel {
    RGB color_sum = RGB(0.0f);       // Accumulated Radiance (premultiplied by alpha)
    float alpha_sum = 0.0f;          // Accumulated coverage
    float weight_sum = 0.0f;         // Total weight (filter weight * count)
    std::atomic<int> deep_head{-1};  // Head of linked list

    // Adaptive sampling state
    RGB color_sq_sum = RGB(0.0f);  // Sum of squared per-sample RGB (for variance)
    int sample_count = 0;          // Actual samples taken
    bool converged = false;        // Convergence flag
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

    // alpha is the flat-pass coverage for this sample (0=transparent, 1=opaque).
    void AddSample(int x, int y, const RGB& L, float alpha, float weight = 1.0f);
    // Same as AddSample but also tracks squared color for variance estimation.
    void AddAdaptiveSample(int x, int y, const RGB& L, float alpha, float weight = 1.0f);
    // Returns true if the pixel's estimated noise is below the threshold.
    bool IsPixelConverged(int x, int y, float noise_threshold) const;
    int GetPixelSampleCount(int x, int y) const;

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
    std::vector<DeepSegmentNode> deep_pool_;
    std::atomic<size_t> pool_cursor_{0};
};

}  // namespace skwr

#endif  // SKWR_FILM_FILM_H_
