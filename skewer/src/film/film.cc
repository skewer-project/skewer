#include "film/film.h"

#include <atomic>

#include "core/constants.h"
#include "film/image_buffer.h"
#include "integrators/path_sample.h"

namespace skwr {

Film::Film(int width, int height)
    : width_(width),
      height_(height),
      pixels_(width_ * height_),
      deep_pool_(width_ * height_ * 100 * 4) {
    // pixels_.resize(width_ * height_);

    // Pre-allocate pool based on expected usage
    // Estimate: width * height * samples_per_pixel * avg_segments_per_path
    // Example: 1920x1080 * 16spp * 8 segments = ~265M nodes
    // might want to make this configurable
    // deep_pool_.resize(width_ * height_ * 16 * 4);
}

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
        if (seg.z_front >= seg.z_back && seg.z_back != kFarClip) continue;
        if (seg.alpha <= 0.0f && seg.L.IsBlack()) continue;

        // Allocate node from pool
        size_t node_index = pool_cursor_.fetch_add(1, std::memory_order_relaxed);
        if (node_index >= deep_pool_.size()) {
            // could dynamically grow this later
            std::cerr << "Warning: Deep pool exhausted at pixel (" << x << "," << y << ")\n";
            return;
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
    // Atomically prepend the entire chain to the pixel's list
    if (prev_head != -1) {
        int old_head = p.deep_head.load(std::memory_order_relaxed);
        do {
            deep_pool_[prev_head].next = old_head;
        } while (!p.deep_head.compare_exchange_weak(old_head, prev_head, std::memory_order_release,
                                                    std::memory_order_relaxed));
    }
}

std::unique_ptr<DeepImageBuffer> Film::CreateDeepBuffer(const int total_pixel_samples) const {
    // Pass 1: Count samples per pixel
    Imf::Array2D<unsigned int> counts(height_, width_);
    size_t total_segments = 0;

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            unsigned int count = 0;
            int head = GetPixel(x, y).deep_head.load(std::memory_order_acquire);
            while (head != -1) {
                count++;
                head = deep_pool_[head].next;
            }
            counts[y][x] = count;
            total_segments += count;
        }
    }

    // Pass 2: Create the buffer (Allocates the flat memory)
    auto buffer = std::make_unique<DeepImageBuffer>(width_, height_, total_segments, counts);

    // Pass 3: Copy, Sort, and merge segments
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (counts[y][x] == 0) continue;

            // Collect samples for this pixel
            std::vector<DeepSample> segments;
            segments.reserve(counts[y][x]);

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

            // Write to your buffer
            buffer->SetPixel(x, y, segments);
        }
    }

    return buffer;
}

// Helper: Merge overlapping/adjacent segments
std::vector<DeepSample> Film::MergeDeepSegments(const std::vector<DeepSample>& input,
                                                const int total_pixel_samples) const {
    if (input.empty()) return input;

    std::vector<DeepSample> merged;
    merged.reserve(input.size() / 4);  // estimate

    DeepSample current = input[0];
    // Track the previous sample's depth for consecutive-gap detection.
    // Comparing against the previous sample (rather than the cluster start)
    // correctly handles gradual depth changes across curved surfaces.
    float prev_z_front = current.z_front;
    float prev_z_back = current.z_back;

    for (size_t i = 1; i < input.size(); ++i) {
        const DeepSample& next = input[i];
        if (next.alpha <= 0.0f) continue;

        // Depth-relative tolerance: accounts for sub-pixel depth variation
        // caused by surface curvature, ray jittering, and anti-aliasing.
        // At depth 10 → epsilon ≈ 0.01, at depth 100 → epsilon ≈ 0.1
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

        // Safety clamp (though mathematically it shouldn't exceed 1.0 if samples <= total)
        if (seg.alpha > 1.0f) seg.alpha = 1.0f;
    }

    static std::atomic<int> log_count{0};
    if (log_count.fetch_add(1) % 10000 == 0) {  // Log every 10000th pixel
        std::cout << "Merge: " << input.size() << " → " << merged.size() << " segments\n";
        if (merged.size() > 0) {
            std::cout << "  First segment: z=" << merged[0].z_front << " rgb=(" << merged[0].r
                      << "," << merged[0].g << "," << merged[0].b << ")"
                      << " alpha=" << merged[0].alpha << "\n";
        }
    }

    return merged;
}

void Film::WriteImage(const std::string& filename) const {
    // Create a TEMPORARY buffer just for this export
    ImageBuffer temp_buffer(width_, height_);

    // Bake the data (Convert Accumulator -> Output Format)
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Pixel& p = GetPixel(x, y);

            RGB final_color(0, 0, 0);
            if (p.weight_sum > 0) {
                final_color = p.color_sum / p.weight_sum;
            }

            temp_buffer.SetPixel(x, y, final_color);
        }
    }

    // Save to disk
    temp_buffer.WritePPM(filename);
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
