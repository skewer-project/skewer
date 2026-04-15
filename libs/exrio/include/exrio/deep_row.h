#ifndef EXRIO_DEEP_ROW_H
#define EXRIO_DEEP_ROW_H

#include <memory>
#include <vector>

namespace exrio {

/**
 * A single row's worth of deep data with efficient flat storage
 *
 * Stores sample data in a contiguous block optimized for streaming I/O:
 * - 6 floats per sample: R, G, B, A, Z (front depth), ZBack (back depth for volumes)
 * - All samples laid out sequentially for cache efficiency
 * - Per-pixel sample counts and offsets enable rapid random pixel access
 *
 * Memory layout:
 *   all_samples buffer: [sample0_R, sample0_G, ..., sample0_ZBack, sample1_R, ...]
 *   sample_offsets[x]: byte offset into all_samples where pixel x's samples begin
 *   sample_counts[x]: number of samples for pixel x
 *
 * Typical usage in streaming pipelines:
 *   1. Call Allocate(width, sample_counts_array) to initialize
 *   2. Access pixel data via GetPixelData(x) or GetSampleData(x, n)
 *   3. Call Clear() when done to deallocate
 */
class DeepRow {
  public:
    // Using the "One big block" optimization to avoid fragmentation
    std::unique_ptr<float[]> all_samples = nullptr;
    int width = 0;
    std::vector<unsigned int> sample_counts;
    std::vector<size_t> sample_offsets;  // sample_offsets[x] = sum(sample_counts[0..x-1])
    size_t total_samples_in_row = 0;
    size_t current_capacity = 0;

    DeepRow() = default;

    DeepRow(DeepRow&& other) noexcept
        : all_samples(std::move(other.all_samples)),
          width(other.width),
          sample_counts(std::move(other.sample_counts)),
          sample_offsets(std::move(other.sample_offsets)),
          total_samples_in_row(other.total_samples_in_row),
          current_capacity(other.current_capacity) {
        // Reset the primitives in the source object
        other.width = 0;
        other.total_samples_in_row = 0;
        other.current_capacity = 0;
    }

    DeepRow& operator=(DeepRow&& other) noexcept {
        if (this != &other) {
            all_samples = std::move(other.all_samples);
            width = other.width;
            sample_counts = std::move(other.sample_counts);
            sample_offsets = std::move(other.sample_offsets);
            total_samples_in_row = other.total_samples_in_row;
            current_capacity = other.current_capacity;

            other.width = 0;
            other.total_samples_in_row = 0;
            other.current_capacity = 0;
        }
        return *this;
    }

    // Normal Allocate given a size
    // Initializes a row with max capacity; actual sample counts set to 0.
    // Used when max capacity is known but actual counts may vary.
    void Allocate(size_t width, int max_samples) {
        this->width = width;
        sample_counts.assign(width, 0);
        sample_offsets.assign(width, 0);  // all zero; merger updates incrementally
        total_samples_in_row = max_samples;

        size_t required = static_cast<size_t>(max_samples) * 6;
        // make_unique handles 'new float[]' and ensures cleanup
        all_samples = std::make_unique<float[]>(required);
        current_capacity = required;
    }

    // Allocate full block based on actual sample counts for the row
    // Initializes offsets and total_samples based on the provided per-pixel counts.
    // Used for allocating exactly what's needed (no wasted space).
    void Allocate(size_t width, const unsigned int* counts) {
        this->width = width;
        sample_counts.assign(counts, counts + width);
        total_samples_in_row = 0;
        sample_offsets.resize(width);
        for (size_t i = 0; i < width; ++i) {
            sample_offsets[i] = total_samples_in_row;
            total_samples_in_row += sample_counts[i];
        }

        size_t required = total_samples_in_row * 6;
        all_samples = std::make_unique<float[]>(required);
        current_capacity = required;
    }

    /**
     * Get pointer to the nth sample of pixel x
     * @param x Pixel x coordinate (0 to width-1)
     * @param n Sample index (0 to sample_counts[x]-1)
     * @return Pointer to sample data (6 floats: R,G,B,A,Z,ZBack) or nullptr if out of bounds
     */
    float* GetSampleData(int x, int n) const {
        if (n >= static_cast<int>(sample_counts[x])) return nullptr;
        return all_samples.get() + (sample_offsets[x] + n) * 6;
    }

    /**
     * Get pointer to all sample data for pixel x (read-only)
     * @param x Pixel x coordinate (0 to width-1)
     * @return Pointer to first sample of pixel x (6*sample_counts[x] floats total)
     */
    const float* GetPixelData(int x) const {
        return all_samples.get() + sample_offsets[x] * 6;
    }

    /**
     * Get pointer to all sample data for pixel x (writable)
     * @param x Pixel x coordinate (0 to width-1)
     * @return Pointer to first sample of pixel x (6*sample_counts[x] floats total)
     */
    float* GetPixelData(int x) {
        return all_samples.get() + sample_offsets[x] * 6;
    }

    /**
     * Get the number of samples for a given pixel
     * @param x Pixel x coordinate (0 to width-1)
     * @return Sample count for pixel x
     */
    unsigned int GetSampleCount(int x) const {
        return sample_counts[x];
    }

    /**
     * Cleanup and deallocate all sample data
     * After calling Clear(), all pointers obtained via GetPixelData/GetSampleData are invalid.
     */
    void Clear() {
        all_samples.reset();  // Safe memory delete
        sample_counts.clear();
        sample_offsets.clear();
        width = 0;
        total_samples_in_row = 0;
        current_capacity = 0;
    }

    // Copy not allowed (inefficient for large buffers)
    DeepRow(const DeepRow&) = delete;
    DeepRow& operator=(const DeepRow&) = delete;

    ~DeepRow() = default;

  private:
    void ensureCapacity(size_t required) {
        if (required > current_capacity) {
            all_samples = std::make_unique<float[]>(required);
            current_capacity = required;
        }
    }
};

/**
 * Converts a row of deep data into a flattened RGBA image row
 *
 * Composites all deep samples in a row using front-to-back alpha blending:
 * - For each pixel: blend samples back to front, early-out at ~1.0 alpha
 * - Output layout: [x0_R, x0_G, x0_B, x0_A, x1_R, x1_G, x1_B, x1_A, ...]
 *
 * @param deepRow Input deep row with samples in R,G,B,A,Z,ZBack format
 * @param rgbaOutput Output buffer (resized if needed to width*4 floats)
 */
inline void FlattenRow(const DeepRow& deepRow, std::vector<float>& rgbaOutput) {
    size_t required = static_cast<size_t>(deepRow.width) * 4;
    if (rgbaOutput.size() < required) {
        rgbaOutput.resize(required);
    }
    const float* pixel_data = deepRow.all_samples.get();
    for (int x = 0; x < deepRow.width; ++x) {
        int num_samples = deepRow.sample_counts[x];

        float accR = 0, accG = 0, accB = 0, accA = 0;

        for (int s = 0; s < num_samples; ++s) {
            const float* sPtr = pixel_data + (s * 6);

            float r = sPtr[0];
            float g = sPtr[1];
            float b = sPtr[2];
            float a = sPtr[3];

            // Standard Front-to-Back "Over" compositing
            // weight = (1 - accumulated_alpha) controls contribution of this sample
            float weight = (1.0f - accA);
            accR += r * weight;
            accG += g * weight;
            accB += b * weight;
            accA += a * weight;

            // Early exit: pixel is opaque
            if (accA >= 0.999f) break;
        }

        // Store as RGBA (4 channels)
        rgbaOutput[x * 4 + 0] = accR;
        rgbaOutput[x * 4 + 1] = accG;
        rgbaOutput[x * 4 + 2] = accB;
        rgbaOutput[x * 4 + 3] = accA;

        pixel_data += num_samples * 6;
    }
}

}  // namespace exrio

#endif  // EXRIO_DEEP_ROW_H
