#include "film/film.h"

#include <exrio/deep_writer.h>

#include <atomic>

#include "barkeep.h"
#include "core/math/constants.h"
#include "core/transport/path_sample.h"
#include "film/deep_segment_pool.h"
#include "film/image_buffer.h"

namespace skwr {

namespace bk = barkeep;

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

void Film::AddDeepSample(int x, int y, const PathSample& path_sample) {
    if (path_sample.segments.empty()) return;

    Pixel& p = GetPixel(x, y);

    int prev_head = -1;
    for (int i = path_sample.segments.size() - 1; i >= 0; --i) {
        const DeepSegment& seg = path_sample.segments[i];

        // Skip empty/invalid segments
        if (seg.z_front > seg.z_back && seg.z_back != kFarClip) continue;
        if (seg.alpha <= 0.0f && seg.L.IsBlack()) continue;

        // Allocate node from pool
        size_t node_index = deep_pool_.Allocate();

        // Fill node
        DeepSegmentNode& node = deep_pool_[node_index];
        node.z_front = seg.z_front;
        node.z_back = seg.z_back;
        node.L = seg.L;
        node.alpha = seg.alpha;
        node.next = prev_head;
        prev_head = node_index;
    }

    if (prev_head != -1) {
        int old_head = p.deep_head.load(std::memory_order_relaxed);
        do {
            deep_pool_[prev_head].next = old_head;
        } while (!p.deep_head.compare_exchange_weak(old_head, prev_head, std::memory_order_release,
                                                    std::memory_order_relaxed));
    }
}

exrio::DeepImage Film::BuildDeepImage() const {
    // Pass 1: Count samples per pixel
    std::vector<unsigned int> counts(width_ * height_, 0);

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            unsigned int count = 0;
            int head = GetPixel(x, y).deep_head.load(std::memory_order_acquire);
            while (head != -1) {
                count++;
                head = deep_pool_[head].next;
            }
            counts[y * width_ + x] = count;
        }
    }

    // Pass 2: Create the deep image
    exrio::DeepImage result(width_, height_);

    std::cout << "\nBuilding deep image (scanline-by-scanline)...\n";
    std::atomic<size_t> scanlines_done(0);
    auto bar = bk::ProgressBar(&scanlines_done, {.total = static_cast<size_t>(height_),
                                                 .speed = 1.0,
                                                 .speed_unit = "lines/s",
                                                 .style = bk::ProgressBarStyle::Rich});
    if (height_ > 0) bar->show();

    // Process one scanline at a time to keep transient memory (sorting buffers) small.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Pixel& p = GetPixel(x, y);
            int head = p.deep_head.load(std::memory_order_acquire);
            if (head == -1) continue;

            // Collect samples for THIS pixel only
            std::vector<DeepSample> segments;

            while (head != -1) {
                const DeepSegmentNode& node = deep_pool_[head];
                DeepSample ds;
                ds.z_front = node.z_front;
                ds.z_back = node.z_back;
                ds.r = node.L.r();
                ds.g = node.L.g();
                ds.b = node.L.b();
                ds.alpha = node.alpha;
                segments.push_back(ds);
                head = node.next;
            }

            // Sort by Depth (Required for OpenEXR)
            std::sort(segments.begin(), segments.end(),
                      [](const DeepSample& a, const DeepSample& b) {
                          if (std::abs(a.z_front - b.z_front) < 1e-5f) {
                              return a.z_back < b.z_back;
                          }
                          return a.z_front < b.z_front;
                      });

            segments = MergeDeepSegments(segments, GetPixel(x, y).sample_count);

            // Add to result
            exrio::DeepPixel& pixel = result.pixel(x, y);
            for (const DeepSample& seg : segments) {
                pixel.addSample(
                    exrio::DeepSample(seg.z_front, seg.z_back, seg.r, seg.g, seg.b, seg.alpha));
            }
        }
        ++scanlines_done;
    }

    if (height_ > 0) bar->done();
    return result;
}

std::vector<DeepSample> Film::MergeDeepSegments(const std::vector<DeepSample>& input,
                                                int pixel_sample_count) const {
    if (input.empty()) return input;

    std::vector<DeepSample> merged;
    size_t reserve_size = std::max<size_t>(1, input.size() / 4);
    merged.reserve(reserve_size);

    DeepSample current = input[0];

    for (size_t i = 1; i < input.size(); ++i) {
        const DeepSample& next = input[i];
        if (next.alpha <= 0.0f) continue;

        // epsilon for grouping merge bins
        float depth_epsilon = std::max(0.01f, std::abs(current.z_front) * 0.015f);

        bool same_front = (std::abs(next.z_front - current.z_front) <= depth_epsilon);

        // Never merge a hard surface with a volume
        bool compatible_alpha = (current.alpha > 0.99f) == (next.alpha > 0.99f);

        if (same_front && compatible_alpha) {
            current.r += next.r;
            current.g += next.g;
            current.b += next.b;
            current.alpha += next.alpha;

            // Grow the tail to merge the furthest stochastic scatter distance in this bucket
            current.z_back =
                (current.z_back + next.z_back) * 0.5f;  // average to prevent stretching
        } else {
            merged.push_back(current);
            current = next;
        }
    }

    merged.push_back(current);

    float active_paths = pixel_sample_count;
    float norm = 1.0f / std::max(pixel_sample_count, 1);

    for (auto& seg : merged) {
        // Because every terminated path gave alpha=1.0,
        // the sum of alphas is exactly the number of paths that stopped here.
        float paths_in_bucket = seg.alpha;

        if (active_paths <= 0.0001f) {
            seg.r = 0;
            seg.g = 0;
            seg.b = 0;
            seg.alpha = 0;
            continue;
        }

        // The true Deep EXR opacity is the fraction of *surviving* paths that stopped here
        float true_opacity = std::min(1.0f, paths_in_bucket / active_paths);

        // OpenEXR requires "associated" (pre-multiplied) colors.
        // We divide out the paths in this bucket to get the raw un-occluded color,
        // then multiply by the true_opacity.
        if (paths_in_bucket > 0.0f) {
            seg.r *= norm;
            seg.g *= norm;
            seg.b *= norm;
        }

        seg.alpha = true_opacity;

        // Subtract the paths that stopped here from the active pool
        active_paths -= paths_in_bucket;
    }

    return merged;
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
