#include "film/image_buffer.h"

#include <cassert>

#include "core/color.h"

namespace skwr {

ImageBuffer::ImageBuffer(int width, int height) : width_(width), height_(height) {
    pixels_.resize(width * height);
}

void ImageBuffer::SetPixel(int x, int y, const RGB& color) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    pixels_[y * width_ + x] = color;
}

/*
 * ======================================================================================
 * DeepImageBuffer Implementation
 * ======================================================================================
 *
 * A high-performance container for Deep Compositing samples using a "Flattened"
 * memory layout (Compressed Row Storage style).
 *
 * MEMORY STRATEGY:
 * Unlike a naive `std::vector<std::vector<DeepSample>>` which causes heap fragmentation
 * and poor cache locality, this class packs all samples for the entire image into
 * a single contiguous memory block (`allSamples_`).
 *
 * - pixelOffsets_: A look-up table (prefix sum) mapping pixel index (y*w + x)
 * to its starting position in the main sample buffer.
 * - Sentinel: The offsets array is size (N+1), allowing O(1) size calculation
 * for the last pixel without bounds checking logic.
 *
 * NOTE: This class assumes strict pre-allocation. The number of samples per pixel
 * is fixed at construction time based on the `sampleCounts` map.
 * ======================================================================================
 */

DeepImageBuffer::DeepImageBuffer(int width, int height, size_t totalSamples,
                                 const Imf::Array2D<unsigned int>& sampleCounts)
    : width_(width), height_(height) {
    // At the very least the offsets needs to be allocated
    size_t numPixels = width * height;
    pixelOffsets_.resize(numPixels + 1);  // for sentinel
    allSamples_.resize(totalSamples);

    size_t currentOffset = 0;
    for (size_t i = 0; i < numPixels; ++i) {
        pixelOffsets_[i] = currentOffset;
        currentOffset += sampleCounts[i / width][i % width];
    }
    pixelOffsets_[numPixels] = currentOffset;  // Sentinel
}

int DeepImageBuffer::GetWidth(void) const { return width_; }

int DeepImageBuffer::GetHeight(void) const { return height_; }

void DeepImageBuffer::SetPixel(int x, int y, const std::vector<DeepSample>& newSamples) {
    assert(x >= 0 && x < width_);  // should crash if out of bounds

    size_t idx = y * width_ + x;
    size_t start = pixelOffsets_[idx];
    [[maybe_unused]] size_t end = pixelOffsets_[idx + 1];

    assert(newSamples.size() == (end - start) &&
           "SetPixel called with the wrong number of samples!");  // Safety Check

    // Mem-copy all samples in place
    std::copy(newSamples.begin(), newSamples.end(), allSamples_.begin() + start);
}

DeepPixelView DeepImageBuffer::GetPixel(int x, int y) const {
    assert(x >= 0 && x < width_);  // crash if out of bounds

    size_t idx = y * width_ + x;
    size_t start = pixelOffsets_[idx];
    size_t end = pixelOffsets_[idx + 1];

    return {&allSamples_[start], end - start};
}

}  // namespace skwr
