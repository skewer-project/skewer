#include "film/film.h"

#include <exrio/deep_writer.h>

#include <atomic>

#include "barkeep.h"
#include "core/constants.h"
#include "film/deep_segment_pool.h"
#include "integrators/path_sample.h"

namespace skwr {

namespace bk = barkeep;

Film::Film(int width, int height)
    : width_(width), height_(height), pixels_(width_ * height_) {}

void Film::AddSample(int x, int y, const RGB& L, float alpha, float weight) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;

    Pixel& p = GetPixel(x, y);

    p.color_sum += L * weight;
    p.alpha_sum += alpha * weight;
    p.weight_sum += weight;
}

void Film::AddDeepSample(int x, int y, const PathSample& path_sample) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    if (path_sample.segments.empty()) return;

    Pixel& p = GetPixel(x, y);

    int prev_head = -1;
    for (int i = path_sample.segments.size() - 1; i >= 0; --i) {
        const DeepSegment& seg = path_sample.segments[i];

        if (seg.z_front >= seg.z_back && seg.z_back != kFarClip) continue;
        if (seg.alpha <= 0.0f && seg.L.IsBlack()) continue;

        // Allocate node from pool
        size_t node_index = deep_pool_.Allocate();
        
        // Safety: If pool is full, we must stop adding segments for this path
        if (node_index == static_cast<size_t>(-1)) {
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true)) {
                std::cerr << "[SKEWER] Warning: Deep segment pool hit its safety limit. Dropping samples.\n";
            }
            break; 
        }

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

exrio::DeepImage Film::BuildDeepImage(const int total_pixel_samples) const {
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
            
            // Limit per-pixel complexity to avoid massive sorting/merging costs
            const int kMaxSegmentsPerPixel = 128; 
            int count = 0;
            while (head != -1 && count < kMaxSegmentsPerPixel) {
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
                count++;
            }

            // Sort by Depth (Required for OpenEXR)
            std::sort(segments.begin(), segments.end(),
                      [](const DeepSample& a, const DeepSample& b) {
                          if (std::abs(a.z_front - b.z_front) < 1e-5f) {
                              return a.z_back < b.z_back;
                          }
                          return a.z_front < b.z_front;
                      });

            segments = MergeDeepSegments(segments, total_pixel_samples);

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
                                                const int total_pixel_samples) const {
    if (input.empty()) return input;

    std::vector<DeepSample> merged;
    merged.reserve(input.size() / 4);

    DeepSample current = input[0];
    float prev_z_front = current.z_front;
    float prev_z_back = current.z_back;

    for (size_t i = 1; i < input.size(); ++i) {
        const DeepSample& next = input[i];
        if (next.alpha <= 0.0f) continue;

        float depth_epsilon = std::max(1e-2f, std::abs(prev_z_front) * 1e-3f);
        bool same_depth = (std::abs(next.z_front - prev_z_front) < depth_epsilon) &&
                          (std::abs(next.z_back - prev_z_back) < depth_epsilon);

        if (same_depth) {
            current.r += next.r;
            current.g += next.g;
            current.b += next.b;
            current.alpha += next.alpha;
        } else {
            merged.push_back(current);
            current = next;
        }

        prev_z_front = next.z_front;
        prev_z_back = next.z_back;
    }

    merged.push_back(current);
    float norm = 1.0f / total_pixel_samples;

    for (auto& seg : merged) {
        seg.r *= norm;
        seg.g *= norm;
        seg.b *= norm;
        seg.alpha *= norm;
        if (seg.alpha > 1.0f) seg.alpha = 1.0f;
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
