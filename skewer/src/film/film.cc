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

Film::Film(int width, int height)
    : width_(width), height_(height), pixels_(width_ * height_), deep_pool_(1) {}

void Film::AddSample(int x, int y, const RGB& L, float alpha, float weight) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;

    Pixel& p = GetPixel(x, y);

    // Thread-safe accumulation (works because RGB is just floats)
    // For true safety, you might want to use atomics or accept some race conditions
    // In practice, the races are benign (slightly wrong accumulated values)
    p.color_sum += L * weight;
    p.alpha_sum += alpha * weight;
    p.weight_sum += weight;
}

void Film::AddDeepSample(int x, int y, const PathSample& path_sample) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    if (path_sample.segments.empty()) return;

    Pixel& p = GetPixel(x, y);

    int prev_head = -1;
    // reverse code for simplicity rn but it's not great for locality..
    for (int i = path_sample.segments.size() - 1; i >= 0; --i) {
        const DeepSegment& seg = path_sample.segments[i];

        // Skip empty/invalid segments
        if (seg.z_front > seg.z_back && seg.z_back != kFarClip) continue;
        if (seg.alpha <= 0.0f && seg.L.IsBlack()) continue;

        // Allocate node from pool (grows automatically)
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
    // Atomically prepend the entire chain to the pixel's list
    if (prev_head != -1) {
        int old_head = p.deep_head.load(std::memory_order_relaxed);
        do {
            deep_pool_[prev_head].next = old_head;
        } while (!p.deep_head.compare_exchange_weak(old_head, prev_head, std::memory_order_release,
                                                    std::memory_order_relaxed));
    }
}

exrio::DeepImage Film::BuildDeepImage(const int total_pixel_samples) const {
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

    // Pass 3: Copy, Sort, and merge segments
    std::cout << "\nBuilding deep image\n";
    std::atomic<size_t> pixels_done(0);
    size_t total_pixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    auto bar = bk::ProgressBar(&pixels_done, {.total = total_pixels,
                                              .speed = 0.2,
                                              .speed_unit = "px/s",
                                              .style = bk::ProgressBarStyle::Rich});
    if (total_pixels > 0) bar->show();

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (counts[y * width_ + x] == 0) {
                ++pixels_done;
                continue;
            }

            // Collect samples for this pixel
            std::vector<DeepSample> segments;
            segments.reserve(counts[y * width_ + x]);

            int head = GetPixel(x, y).deep_head.load(std::memory_order_acquire);
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
                              return a.z_back < b.z_back;  // Tiebreaker
                          }
                          return a.z_front < b.z_front;
                      });

            segments = MergeDeepSegments(segments, total_pixel_samples);

            // Convert to deep_compositor::DeepSample and add to result
            exrio::DeepPixel& pixel = result.pixel(x, y);
            for (const DeepSample& seg : segments) {
                pixel.addSample(
                    exrio::DeepSample(seg.z_front, seg.z_back, seg.r, seg.g, seg.b, seg.alpha));
            }
            ++pixels_done;
        }
    }

    if (total_pixels > 0) bar->done();

    return result;
}

// Helper: Merge overlapping/adjacent segments
std::vector<DeepSample> Film::MergeDeepSegments(const std::vector<DeepSample>& input,
                                                const int total_pixel_samples) const {
    if (input.empty()) return input;

    std::vector<DeepSample> merged;
    merged.reserve(input.size() / 4);  // estimate

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

    float active_paths = total_pixel_samples;

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
            float rgb_norm = true_opacity / paths_in_bucket;
            seg.r *= rgb_norm;
            seg.g *= rgb_norm;
            seg.b *= rgb_norm;
        }

        seg.alpha = true_opacity;

        // Subtract the paths that stopped here from the active pool
        active_paths -= paths_in_bucket;
    }

    return merged;
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

    exrio::writePNG(rgba, width_, height_, filename);
    std::cout << "Wrote image to " << filename << "\n";
}

std::unique_ptr<FlatImageBuffer> Film::CreateFlatBuffer() const {
    auto buf = std::make_unique<FlatImageBuffer>(width_, height_);

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Pixel& p = GetPixel(x, y);

            RGB color(0.0f);
            float alpha = 0.0f;

            if (p.weight_sum > 0) {
                // color_sum is already premultiplied (misses contribute 0 to both),
                // so dividing by weight gives premultiplied average.
                color = p.color_sum / p.weight_sum;
                alpha = p.alpha_sum / p.weight_sum;
            }

            buf->SetPixel(x, y, color, alpha);
        }
    }

    return buf;
}

}  // namespace skwr
