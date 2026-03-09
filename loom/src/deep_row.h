#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

// A single row's worth of deep data for one file
struct DeepRow {
    // Using the "One big block" optimization to avoid fragmentation
    std::unique_ptr<float[]> allSamples = nullptr;
    int width = 0;
    std::vector<unsigned int> sampleCounts;
    size_t totalSamplesInRow = 0;
    size_t currentCapacity = 0;

    DeepRow() = default;

    DeepRow(DeepRow&& other) noexcept { *this = std::move(other); }

    DeepRow& operator=(DeepRow&& other) noexcept {
        if (this != &other) {
            // unique_ptr handles deleting our old memory automatically here
            allSamples = std::move(other.allSamples);

            // Steal the primitive metadata
            width = other.width;
            sampleCounts = std::move(other.sampleCounts);
            totalSamplesInRow = other.totalSamplesInRow;
            currentCapacity = other.currentCapacity;

            // Reset the 'other' object to a clean state
            other.width = 0;
            other.totalSamplesInRow = 0;
            other.currentCapacity = 0;
        }
        return *this;
    }

    // Normal Allocate given a size
    void allocate(size_t width, int maxSamples) {
        this->width = width;
        sampleCounts.assign(width, 0);
        totalSamplesInRow = maxSamples;

        size_t required = static_cast<size_t>(maxSamples) * 6;
        // make_unique handles 'new float[]' and ensures cleanup
        allSamples = std::make_unique<float[]>(required);
        currentCapacity = required;
    }

    // Allocate full block based on actual sample counts for the row
    void allocate(size_t width, const unsigned int* counts) {
        this->width = width;
        sampleCounts.assign(counts, counts + width);
        totalSamplesInRow = 0;
        for (auto c : sampleCounts) totalSamplesInRow += c;

        size_t required = totalSamplesInRow * 6;
        allSamples = std::make_unique<float[]>(required);
        currentCapacity = required;
    }

    // Get pointer to the nth sample of pixel x
    float* getSampleData(int x, int n) const {
        size_t pixelStartOffset = 0;
        for (int i = 0; i < x; ++i) pixelStartOffset += sampleCounts[i];

        if (n >= static_cast<int>(sampleCounts[x])) return nullptr;

        size_t finalOffset = (pixelStartOffset + n) * 6;
        // Use .get() to access the raw pointer inside the smart pointer
        return allSamples.get() + finalOffset;
    }

    const float* getPixelData(int x) const {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) offset += sampleCounts[i];
        return allSamples.get() + (offset * 6);
    }

    // Non-const version for writing
    float* getPixelData(int x) {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) offset += sampleCounts[i];
        return allSamples.get() + (offset * 6);
    }

    unsigned int getSampleCount(int x) const { return sampleCounts[x]; }

    // Cleanup
    void clear() {
        allSamples.reset();  // Safe memory delete
        sampleCounts.clear();
        width = 0;
        totalSamplesInRow = 0;
        currentCapacity = 0;
    }

    DeepRow(const DeepRow&) = delete;

    // DeepRow& operator=(const DeepRow& ) = delete;

    ~DeepRow() = default;

  private:
    void ensureCapacity(size_t required) {
        if (required > currentCapacity) {
            allSamples = std::make_unique<float[]>(required);
            currentCapacity = required;
        }
    }
};

// Converts a row of deep data into a flattened RGBA image row
inline void flattenRow(const DeepRow& deepRow, std::vector<float>& rgbaOutput) {
    for (int x = 0; x < deepRow.width; ++x) {
        const float* pixelData = deepRow.getPixelData(x);
        int numSamples = deepRow.sampleCounts[x];

        float accR = 0, accG = 0, accB = 0, accA = 0;

        for (int s = 0; s < numSamples; ++s) {
            const float* sPtr = pixelData + (s * 6);

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