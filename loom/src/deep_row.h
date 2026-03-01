#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <ImfDeepFrameBuffer.h> // OpenEXR

// A single row's worth of deep data for one file
struct DeepRow {
    // Using the "One big block" optimization to avoid fragmentation
    float* allSamples = nullptr; 
    int width = 0;
    std::vector<unsigned int> sampleCounts;
    size_t totalSamplesInRow = 0;
    size_t currentCapacity = 0;


    // Normal Allocate given a size
    void allocate(size_t width, int maxSamples) {
        if (allSamples != nullptr) {
            delete[] allSamples; // FREE OLD ROW DATA
            allSamples = nullptr;
            printf("Warning: Re-allocating DeepRow for width %zu. Previous data has been freed.\n", width);
        }
        this->width = width;
        // ensureCapacity((size_t)maxSamples * 6);
        sampleCounts.assign(width, 0); 
        totalSamplesInRow = 0;
        totalSamplesInRow = maxSamples;
        allSamples = new float[maxSamples * 6]; // Full Block Allocation 
        currentCapacity = maxSamples * 6; //! MUST CHANGE THIS TO WORK WITH CAPACITY LOGIC
    }


    // Allocate full block based on actual sample counts for the row
    void allocate(size_t width, const unsigned int* counts) {
        if (allSamples != nullptr) {
            delete[] allSamples; // FREE OLD ROW DATA
            allSamples = nullptr;
            // printf("Warning: Re-allocating DeepRow for width %zu. Previous data has been freed.\n", width);
        }
        this->width = width;
        sampleCounts.assign(counts, counts + width); // Copy counts into the vector
        totalSamplesInRow = 0;
        for(auto c : sampleCounts) totalSamplesInRow += c;
        
        // Allocate one contiguous block for all RGBAZ data in this row
        allSamples = new float[totalSamplesInRow * 6]; // Full Block Allocation
        currentCapacity = totalSamplesInRow * 6;
        // printf("Allocated DeepRow: width=%zu, totalSamples=%zu, capacity=%zu\n", width, totalSamplesInRow, currentCapacity);
    }

    // Get pointer to the nth sample of pixel x
     float* getSampleData(int x, int n) const {
        // 1. Find the start of pixel x (same logic as getPixelData)
        size_t pixelStartOffset = 0;
        for (int i = 0; i < x; ++i) {
            pixelStartOffset += sampleCounts[i];
        }

        // 2. Bounds check: ensure the nth sample actually exists for this pixel
        if (n >= static_cast<int>(sampleCounts[x])) {
            return nullptr; // Or handle error: requested sample index out of range
        }

        // 3. Final Offset: (Start of Pixel + n samples) * 6 channels
        size_t finalOffset = (pixelStartOffset + n) * 6;

        return allSamples + finalOffset;
    }


    const float* getPixelData(int x) const {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) {
            offset += sampleCounts[i];
        }
        // Multiply by 6 because of RGBAZ interleaving
        return allSamples + (offset * 6);
    }

    // Non-const version for writing
    float* getPixelData(int x) {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) offset += sampleCounts[i];
        return allSamples + (offset * 6);
    }

    unsigned int getSampleCount(int x) const {
        return sampleCounts[x];
    }


    // Cleanup
    void clear() {
        if (allSamples) delete[] allSamples;
        allSamples = nullptr;
        sampleCounts.clear();
    }
    
    ~DeepRow() { clear(); }
private:

    void ensureCapacity(size_t required) {
        if (required > currentCapacity) {
            if (allSamples) delete[] allSamples;
            allSamples = new float[required];
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