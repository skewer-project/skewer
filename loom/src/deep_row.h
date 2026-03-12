#ifndef LOOM_SRC_DEEP_ROW_H
#define LOOM_SRC_DEEP_ROW_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

// A single row's worth of deep data for one file
struct DeepRow {
    // Using the "One big block" optimization to avoid fragmentation
    std::unique_ptr<float[]> all_samples = nullptr;
    int width = 0;
    std::vector<unsigned int> sample_counts;
    size_t total_samples_in_row = 0;
    size_t current_capacity = 0;

    DeepRow() = default;

    DeepRow(DeepRow&& other) noexcept
        : all_samples(std::move(other.all_samples)),
          width(other.width),
          sample_counts(std::move(other.sample_counts)),
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
            total_samples_in_row = other.total_samples_in_row;
            current_capacity = other.current_capacity;

            other.width = 0;
            other.total_samples_in_row = 0;
            other.current_capacity = 0;
        }
        return *this;
    }

    // Normal Allocate given a size
    void Allocate(size_t width, int max_samples) {
        this->width = width;
        sample_counts.assign(width, 0);
        total_samples_in_row = max_samples;

        size_t required = static_cast<size_t>(max_samples) * 6;
        // make_unique handles 'new float[]' and ensures cleanup
        all_samples = std::make_unique<float[]>(required);
        current_capacity = required;
    }

    // Allocate full block based on actual sample counts for the row
    void Allocate(size_t width, const unsigned int* counts) {
        this->width = width;
        sample_counts.assign(counts, counts + width);
        total_samples_in_row = 0;
        for (auto c : sample_counts) total_samples_in_row += c;

        size_t required = total_samples_in_row * 6;
        all_samples = std::make_unique<float[]>(required);
        current_capacity = required;
    }

    // Get pointer to the nth sample of pixel x
    float* GetSampleData(int x, int n) const {
        size_t pixel_start_offset = 0;
        for (int i = 0; i < x; ++i) pixel_start_offset += sample_counts[i];

        if (n >= static_cast<int>(sample_counts[x])) return nullptr;

        size_t final_offset = (pixel_start_offset + n) * 6;
        // Use .get() to access the raw pointer inside the smart pointer
        return all_samples.get() + final_offset;
    }

    const float* GetPixelData(int x) const {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) offset += sample_counts[i];
        return all_samples.get() + (offset * 6);
    }

    // Non-const version for writing
    float* GetPixelData(int x) {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) offset += sample_counts[i];
        return all_samples.get() + (offset * 6);
    }

    unsigned int GetSampleCount(int x) const { return sample_counts[x]; }

    // Cleanup
    void Clear() {
        all_samples.reset();  // Safe memory delete
        sample_counts.clear();
        width = 0;
        total_samples_in_row = 0;
        current_capacity = 0;
    }

    DeepRow(const DeepRow&) = delete;

    // DeepRow& operator=(const DeepRow& ) = delete;

    ~DeepRow() = default;

  private:
    void ensureCapacity(size_t required) {
        if (required > current_capacity) {
            all_samples = std::make_unique<float[]>(required);
            current_capacity = required;
        }
    }
};

// Converts a row of deep data into a flattened RGBA image row
inline void FlattenRow(const DeepRow& deepRow, std::vector<float>& rgbaOutput) {
    for (int x = 0; x < deepRow.width; ++x) {
        const float* pixel_data = deepRow.GetPixelData(x);
        int num_samples = deepRow.sample_counts[x];

        float accR = 0, accG = 0, accB = 0, accA = 0;

        for (int s = 0; s < num_samples; ++s) {
            const float* sPtr = pixel_data + (s * 6);

            float r = sPtr[0];
            float g = sPtr[1];
            float b = sPtr[2];
            float a = sPtr[3];

            // Standard Front-to-Back "Over"
            float weight = (1.0f - accA);
            accR += r * weight;
            accG += g * weight;
            accB += b * weight;
            accA += a * weight;

            if (accA >= 0.999f) break;
        }

        // Store as RGBA (4 channels)
        rgbaOutput[x * 4 + 0] = accR;
        rgbaOutput[x * 4 + 1] = accG;
        rgbaOutput[x * 4 + 2] = accB;
        rgbaOutput[x * 4 + 3] = accA;
    }
}

#endif  // LOOM_SRC_DEEP_ROW_H