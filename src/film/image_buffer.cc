#include "film/image_buffer.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <ostream>

#include "core/spectrum.h"

namespace skwr {

ImageBuffer::ImageBuffer(int width, int height) : width_(width), height_(height) {
    pixels_.resize(width * height);
}

void ImageBuffer::SetPixel(int x, int y, const Spectrum& s) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    pixels_[y * width_ + x] = s;
}

// For debug and testing purposes, we can keep this PPM writer but
// ultimately we should move this to a separate src/io/image_io.h or something
void ImageBuffer::WritePPM(const std::string& filename) const {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Error: Could not open " << filename << " for writing.\n";
        return;
    }

    // PPM Header: P3 = ASCII RGB, then width, height, max_val
    out << "P3\n" << width_ << " " << height_ << "\n255\n";

    for (const auto& pixel : pixels_) {
        Color c = pixel.ToColor();
        c.ApplyGammaCorrection();
        c.Clamp(0.0f, 1.0f);
        // Convert float (0.0-1.0) to int (0-255)
        int ir = static_cast<int>(255.999 * c.r());
        int ig = static_cast<int>(255.999 * c.g());
        int ib = static_cast<int>(255.999 * c.b());

        out << ir << " " << ig << " " << ib << "\n";
    }

    out.close();
    std::cout << "Wrote image to " << filename << "\n";
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
    size_t end = pixelOffsets_[idx + 1];

    size_t slotSize = end - start;
    assert(newSamples.size() == slotSize &&
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

// A mutable version for setting samples at a given pixel
MutableDeepPixelView DeepImageBuffer::GetMutablePixel(int x, int y) {
    assert(x >= 0 && x < width_);

    size_t idx = y * width_ + x;
    size_t start = pixelOffsets_[idx];
    size_t end = pixelOffsets_[idx + 1];

    return {&allSamples_[start], end - start};
}

}  // namespace skwr
