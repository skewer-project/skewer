#ifndef SKWR_FILM_FILM_H_
#define SKWR_FILM_FILM_H_

#include <exrio/deep_image.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "core/color/color.h"
#include "core/containers/bounded_array.h"
#include "core/cpu_config.h"
#include "core/transport/deep_segment.h"
#include "film/deep_bucket.h"
#include "film/image_buffer.h"

namespace skwr {

// Pixel buckets are allocated inline (kMaxDeepBuckets entries always reserved)
// rather than via a global pool. Render is tile-exclusive per thread, so no
// atomics are needed for sample writes.
struct Pixel {
    RGB color_sum = RGB(0.0f);
    float alpha_sum = 0.0f;
    float weight_sum = 0.0f;
    int sample_count = 0;
    RGB color_sq_sum = RGB(0.0f);
    BoundedArray<DeepBucket, kMaxDeepBuckets> deep_buckets;
};

// Aggregate counters describing how the deep-bucket cap behaved across a
// render. Used to validate the kMaxDeepBuckets sizing in production.
struct DeepBucketStats {
    std::size_t pixels_with_buckets = 0;
    std::size_t total_buckets = 0;
    std::size_t peak_buckets_per_pixel = 0;
    std::size_t forced_evictions = 0;
};

class Film {
  public:
    Film(int width, int height);

    // alpha is the flat-pass coverage for this sample (0=transparent, 1=opaque).
    void AddSample(int x, int y, const RGB& L, float alpha, float weight = 1.0f);

    // accumulates both moments for variance tracking.
    void AddAdaptiveSample(int x, int y, const RGB& L, float alpha, float weight);

    // convergence check, should called every adaptive_step samples.
    bool IsPixelConverged(int x, int y, float noise_threshold) const;

    void AddDeepSample(int x, int y, const BoundedArray<DeepSegment, kMaxDeepSegments>& segments);

    // Saves to disk (PNG, EXR)
    void WriteImage(const std::string& filename) const;

    // Debug: writes a heatmap PNG showing sample count per pixel.
    // Pixels are colored blue (few samples) to red (max_samples).
    void WriteSampleMap(const std::string& filename, int max_samples) const;

    // Streaming deep EXR writer: walks rows, emits one scanline at a time, and
    // frees per-row buckets as it goes. This is the production path; peak
    // memory is dominated by the bounded per-pixel buckets, not by a full
    // intermediate DeepImage.
    void WriteDeepEXRStreaming(const std::string& filename);

    // Test/debug only: builds the full DeepImage in memory. Costs roughly the
    // same as the bounded-bucket storage itself and should not be used on
    // large frames in production — prefer WriteDeepEXRStreaming.
    exrio::DeepImage BuildDeepImage() const;

    // Builds a flat RGBA buffer suitable for export as a compositing-friendly EXR.
    // Colors are premultiplied; alpha reflects average coverage per pixel.
    std::unique_ptr<FlatImageBuffer> CreateFlatBuffer() const;

    DeepBucketStats GetDeepBucketStats() const;

    int width() { return width_; }
    int height() { return height_; }

  private:
    Pixel& GetPixel(int x, int y) { return pixels_[y * width_ + x]; }
    const Pixel& GetPixel(int x, int y) const { return pixels_[y * width_ + x]; }

    // Convert a pixel's merged buckets into normalized, sorted deep samples
    // ready to hand to exrio. Applies the back-to-front true_opacity pass.
    void BuildPixelDeepSamples(const Pixel& p, std::vector<exrio::DeepSample>& out) const;

    int width_, height_;
    std::vector<Pixel> pixels_;
    std::size_t forced_evictions_ = 0;
};

}  // namespace skwr

#endif  // SKWR_FILM_FILM_H_
