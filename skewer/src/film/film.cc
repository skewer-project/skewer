#include "film/film.h"

#include <exrio/deep_writer.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "barkeep.h"
#include "core/containers/bounded_array.h"
#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/progress_config.h"
#include "core/transport/deep_segment.h"
#include "film/deep_bucket.h"
#include "film/image_buffer.h"

namespace skwr {

namespace bk = barkeep;

namespace {

inline DeepAlphaClass ClassifyAlpha(float alpha) {
    return (alpha > 0.99f) ? DeepAlphaClass::Surface : DeepAlphaClass::Volume;
}

}  // namespace

Film::Film(int width, int height) : width_(width), height_(height), pixels_(width_ * height_) {}

void Film::AddSample(int x, int y, const RGB& L, float alpha, float weight) {
    Pixel& p = GetPixel(x, y);

    p.color_sum += L * weight;
    p.alpha_sum += alpha * weight;
    p.weight_sum += weight;
    ++p.sample_count;
}

void Film::AddAdaptiveSample(int x, int y, const RGB& L, float alpha, float weight) {
    Pixel& p = pixels_[y * width_ + x];
    p.color_sum += L * weight;
    p.alpha_sum += alpha * weight;
    p.weight_sum += weight;
    ++p.sample_count;
    p.color_sq_sum += L * L * weight;
}

bool Film::IsPixelConverged(int x, int y, float noise_threshold) const {
    const Pixel& p = pixels_[y * width_ + x];
    float n = static_cast<float>(p.sample_count);
    if (n < 2.0f) return false;

    RGB mean = p.color_sum / n;
    RGB mean_sq = p.color_sq_sum / n;
    float var_r = std::max(0.0f, mean_sq.r() - mean.r() * mean.r());
    float var_g = std::max(0.0f, mean_sq.g() - mean.g() * mean.g());
    float var_b = std::max(0.0f, mean_sq.b() - mean.b() * mean.b());

    float mean_lum = Rec709::kWeightRed * mean.r() + Rec709::kWeightGreen * mean.g() +
                     Rec709::kWeightBlue * mean.b();

    float var_lum = (Rec709::kWeightRedSquared)*var_r + (Rec709::kWeightGreenSquared)*var_g +
                    (Rec709::kWeightBlueSquared)*var_b;

    // Clamp luminance floor to 0.5 (Cycles approach) so dark pixels
    // use an absolute threshold instead of blowing up relative noise.
    float noise = std::sqrt(var_lum / n);
    return noise / std::max(mean_lum, 0.5f) < noise_threshold;
}

void Film::AddDeepSample(int x, int y,
                         const BoundedArray<DeepSegment, kMaxDeepSegments>& segments) {
    if (segments.empty()) return;

    Pixel& p = GetPixel(x, y);

    for (size_t i = 0; i < segments.size(); ++i) {
        const DeepSegment& seg = segments[i];

        // Skip empty/invalid segments
        if (seg.z_front > seg.z_back && seg.z_back != RenderConstants::kFarClip) continue;
        if (seg.alpha <= 0.0f && seg.L.IsBlack()) continue;

        const DeepAlphaClass cls = ClassifyAlpha(seg.alpha);
        const float eps = std::max(0.01f, std::abs(seg.z_front) * 0.015f);

        // 1. Look for a compatible bucket (same depth + same alpha class).
        int compat_idx = -1;
        float compat_dist = std::numeric_limits<float>::max();
        for (size_t j = 0; j < p.deep_buckets.size(); ++j) {
            const DeepBucket& b = p.deep_buckets[j];
            if (b.alpha_class != cls) continue;
            const float d = std::abs(b.z_front - seg.z_front);
            if (d > eps) continue;
            if (d < compat_dist) {
                compat_dist = d;
                compat_idx = static_cast<int>(j);
            }
        }

        if (compat_idx >= 0) {
            DeepBucket& b = p.deep_buckets[compat_idx];
            b.sum_r += seg.L.r();
            b.sum_g += seg.L.g();
            b.sum_b += seg.L.b();
            b.sum_alpha += seg.alpha;
            // Average to keep the tail from stretching across stochastic scatters.
            b.z_back = (b.z_back + seg.z_back) * 0.5f;
            continue;
        }

        // 2. Append a new bucket if we still have room.
        if (p.deep_buckets.size() < kMaxDeepBuckets) {
            DeepBucket nb;
            nb.z_front = seg.z_front;
            nb.z_back = seg.z_back;
            nb.sum_r = seg.L.r();
            nb.sum_g = seg.L.g();
            nb.sum_b = seg.L.b();
            nb.sum_alpha = seg.alpha;
            nb.alpha_class = cls;
            p.deep_buckets.push_back(nb);
            continue;
        }

        // 3. Forced eviction: merge into the nearest-by-z_front bucket
        // regardless of alpha class. Loses some class purity but keeps every
        // sample's contribution accounted for.
        ++forced_evictions_;
        int nearest_idx = 0;
        float nearest_dist = std::abs(p.deep_buckets[0].z_front - seg.z_front);
        for (size_t j = 1; j < p.deep_buckets.size(); ++j) {
            const float d = std::abs(p.deep_buckets[j].z_front - seg.z_front);
            if (d < nearest_dist) {
                nearest_dist = d;
                nearest_idx = static_cast<int>(j);
            }
        }
        DeepBucket& b = p.deep_buckets[nearest_idx];
        b.sum_r += seg.L.r();
        b.sum_g += seg.L.g();
        b.sum_b += seg.L.b();
        b.sum_alpha += seg.alpha;
        b.z_back = (b.z_back + seg.z_back) * 0.5f;
    }
}

void Film::BuildPixelDeepSamples(const Pixel& p, std::vector<exrio::DeepSample>& out) const {
    out.clear();
    if (p.deep_buckets.empty()) return;

    // Copy buckets to a sortable scratch vector. Bucket count per pixel is
    // bounded by kMaxDeepBuckets, so this is small.
    std::vector<DeepBucket> sorted;
    sorted.reserve(p.deep_buckets.size());
    for (size_t i = 0; i < p.deep_buckets.size(); ++i) {
        sorted.push_back(p.deep_buckets[i]);
    }
    std::sort(sorted.begin(), sorted.end(), [](const DeepBucket& a, const DeepBucket& b) {
        if (std::abs(a.z_front - b.z_front) < 1e-5f) {
            return a.z_back < b.z_back;
        }
        return a.z_front < b.z_front;
    });

    const float pixel_sample_count = static_cast<float>(p.sample_count);
    if (pixel_sample_count <= 0.0f) return;

    const float norm = 1.0f / std::max(p.sample_count, 1);
    float active_paths = pixel_sample_count;

    out.reserve(sorted.size());
    for (const DeepBucket& b : sorted) {
        const float paths_in_bucket = b.sum_alpha;

        if (active_paths <= 0.0001f) {
            // No remaining coverage to allocate to this bucket.
            continue;
        }

        const float true_opacity = std::min(1.0f, paths_in_bucket / active_paths);

        // OpenEXR requires associated (premultiplied) colors. Dividing the
        // raw radiance sum by the pixel's total sample count yields the
        // average per-sample contribution; alpha then carries the fraction
        // of remaining paths terminating in this bucket.
        exrio::DeepSample ds;
        ds.depth = b.z_front;
        ds.depth_back = b.z_back;
        ds.red = b.sum_r * norm;
        ds.green = b.sum_g * norm;
        ds.blue = b.sum_b * norm;
        ds.alpha = true_opacity;
        out.push_back(ds);

        active_paths -= paths_in_bucket;
    }
}

exrio::DeepImage Film::BuildDeepImage() const {
    exrio::DeepImage result(width_, height_);

    std::vector<exrio::DeepSample> per_pixel;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            BuildPixelDeepSamples(GetPixel(x, y), per_pixel);
            if (per_pixel.empty()) continue;
            exrio::DeepPixel& pixel = result.pixel(x, y);
            for (const exrio::DeepSample& ds : per_pixel) {
                pixel.addSample(ds);
            }
        }
    }
    return result;
}

void Film::WriteDeepEXRStreaming(const std::string& filename) {
    if (width_ <= 0 || height_ <= 0) {
        throw std::runtime_error("Film::WriteDeepEXRStreaming: invalid dimensions");
    }

    exrio::DeepScanlineWriter writer(width_, height_, filename);

    std::cout << "\nWriting deep EXR (streaming, scanline-by-scanline)...\n";
    std::atomic<size_t> scanlines_done(0);
    const auto progress_mode = GetProgressOutputMode();
    auto bar = bk::ProgressBar(&scanlines_done, {.total = static_cast<size_t>(height_),
                                                 .speed = 1.0,
                                                 .speed_unit = "lines/s",
                                                 .style = progress_mode.style,
                                                 .interval = progress_mode.interval,
                                                 .no_tty = progress_mode.no_tty});
    if (height_ > 0) bar->show();

    std::vector<unsigned int> row_counts(width_);
    std::vector<exrio::DeepSample> row_samples;
    std::vector<exrio::DeepSample> per_pixel;

    for (int y = 0; y < height_; ++y) {
        row_samples.clear();

        for (int x = 0; x < width_; ++x) {
            BuildPixelDeepSamples(GetPixel(x, y), per_pixel);
            row_counts[x] = static_cast<unsigned int>(per_pixel.size());
            for (const exrio::DeepSample& ds : per_pixel) {
                row_samples.push_back(ds);
            }
        }

        writer.writeScanline(row_counts, row_samples);

        // Free the row's buckets now that they're written. Halves resident
        // deep memory by the time the writer reaches the bottom of the image.
        for (int x = 0; x < width_; ++x) {
            GetPixel(x, y).deep_buckets.clear();
        }

        ++scanlines_done;
    }

    if (height_ > 0) bar->done();
}

DeepBucketStats Film::GetDeepBucketStats() const {
    DeepBucketStats stats;
    stats.forced_evictions = forced_evictions_;
    for (const Pixel& p : pixels_) {
        const std::size_t n = p.deep_buckets.size();
        if (n == 0) continue;
        ++stats.pixels_with_buckets;
        stats.total_buckets += n;
        if (n > stats.peak_buckets_per_pixel) {
            stats.peak_buckets_per_pixel = n;
        }
    }
    return stats;
}

void Film::WriteSampleMap(const std::string& filename, int max_samples) const {
    std::vector<float> rgba(static_cast<size_t>(width_) * height_ * 4);

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Pixel& p = GetPixel(x, y);
            float t = (max_samples > 0)
                          ? std::min(static_cast<float>(p.sample_count) / max_samples, 1.0f)
                          : 0.0f;

            // Blue (0,0,1) → Green (0,1,0) → Red (1,0,0)
            float r, g, b;
            if (t < 0.5f) {
                float s = t * 2.0f;
                r = 0.0f;
                g = s;
                b = 1.0f - s;
            } else {
                float s = (t - 0.5f) * 2.0f;
                r = s;
                g = 1.0f - s;
                b = 0.0f;
            }

            size_t idx = (static_cast<size_t>(y) * width_ + x) * 4;
            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = 1.0f;
        }
    }

    exrio::writePNG(rgba, width_, height_, filename);
    std::cout << "Wrote sample map to " << filename << "\n";
}

void Film::WriteImage(const std::string& filename) const {
    std::vector<float> rgba(static_cast<size_t>(width_) * height_ * 4);

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Pixel& p = GetPixel(x, y);
            RGB color(0, 0, 0);
            float alpha = 0.0f;
            if (p.weight_sum > 0) {
                color = p.color_sum / p.weight_sum;
                alpha = p.alpha_sum / p.weight_sum;
            }
            size_t idx = (static_cast<size_t>(y) * width_ + x) * 4;
            rgba[idx + 0] = color.r();
            rgba[idx + 1] = color.g();
            rgba[idx + 2] = color.b();
            rgba[idx + 3] = alpha;
        }
    }

    if (filename.ends_with(".exr")) {
        exrio::writeFlatEXR(rgba, width_, height_, filename);
        std::cout << "Wrote flat EXR to " << filename << "\n";
    } else {
        exrio::writePNG(rgba, width_, height_, filename);
        std::cout << "Wrote PNG to " << filename << "\n";
    }
}

std::unique_ptr<FlatImageBuffer> Film::CreateFlatBuffer() const {
    auto buf = std::make_unique<FlatImageBuffer>(width_, height_);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Pixel& p = GetPixel(x, y);
            RGB color(0.0f);
            float alpha = 0.0f;
            if (p.weight_sum > 0) {
                color = p.color_sum / p.weight_sum;
                alpha = p.alpha_sum / p.weight_sum;
            }
            buf->SetPixel(x, y, color, alpha);
        }
    }
    return buf;
}

}  // namespace skwr
