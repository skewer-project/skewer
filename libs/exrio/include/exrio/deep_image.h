#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace exrio {

/**
 * A single deep sample containing depth and premultiplied RGBA values
 */
struct DeepSample {
    float depth;       // Z front (depth from camera)
    float depth_back;  // Z back. Equal to depth for point/hard-surface samples.
    float red;         // Premultiplied red
    float green;       // Premultiplied green
    float blue;        // Premultiplied blue
    float alpha;       // Coverage/opacity

    DeepSample() : depth(0.0f), depth_back(0.0f), red(0.0f), green(0.0f), blue(0.0f), alpha(0.0f) {}

    // Zero-thickness convenience constructor (depth_back = depth)
    DeepSample(float z, float r, float g, float b, float a)
        : depth(z), depth_back(z), red(r), green(g), blue(b), alpha(a) {}

    // Full volumetric constructor
    DeepSample(float z_front, float z_back, float r, float g, float b, float a)
        : depth(z_front), depth_back(z_back), red(r), green(g), blue(b), alpha(a) {}

    bool isVolume() const { return depth_back > depth; }
    float thickness() const { return depth_back - depth; }

    /**
     * Compare samples by depth (for sorting front-to-back),
     * with depth_back as tiebreaker
     */
    bool operator<(const DeepSample& other) const {
        if (depth != other.depth) return depth < other.depth;
        return depth_back < other.depth_back;
    }

    /**
     * Check if two samples are at approximately the same depth range
     */
    bool isNearDepth(const DeepSample& other, float epsilon = 0.001f) const {
        return std::abs(depth - other.depth) < epsilon &&
               std::abs(depth_back - other.depth_back) < epsilon;
    }
};

/**
 * A pixel containing multiple deep samples, sorted by depth
 */
class DeepPixel {
  public:
    DeepPixel() = default;

    /**
     * Add a sample to this pixel, maintaining depth sort order
     */
    void addSample(const DeepSample& sample);

    /**
     * Add multiple samples at once
     */
    void addSamples(const std::vector<DeepSample>& newSamples);

    /**
     * Get the number of samples in this pixel
     */
    size_t sampleCount() const { return samples_.size(); }

    /**
     * Check if this pixel has any samples
     */
    bool isEmpty() const { return samples_.empty(); }

    /**
     * Get all samples (const)
     */
    const std::vector<DeepSample>& samples() const { return samples_; }

    /**
     * Get all samples (mutable)
     */
    std::vector<DeepSample>& samples() { return samples_; }

    /**
     * Get a specific sample by index
     */
    const DeepSample& operator[](size_t index) const { return samples_[index]; }
    DeepSample& operator[](size_t index) { return samples_[index]; }

    /**
     * Clear all samples
     */
    void clear() { samples_.clear(); }

    /**
     * Sort samples by depth (front to back)
     */
    void sortByDepth();

    /**
     * Merge samples that are within epsilon depth of each other
     */
    void mergeSamplesWithinEpsilon(float epsilon = 0.001f);

    /**
     * Get the minimum depth in this pixel
     */
    float minDepth() const;

    /**
     * Get the maximum depth in this pixel
     */
    float maxDepth() const;

    /**
     * Validate that samples are sorted correctly
     */
    bool isValidSortOrder() const;

  private:
    std::vector<DeepSample> samples_;  // Sorted by depth (front to back)
};

/**
 * A 2D deep image containing a grid of deep pixels
 */
class DeepImage {
  public:
    DeepImage();
    DeepImage(int width, int height);

    /**
     * Resize the image (clears all existing data)
     */
    void resize(int width, int height);

    /**
     * Get image dimensions
     */
    int width() const { return width_; }
    int height() const { return height_; }

    /**
     * Access a pixel at (x, y)
     */
    DeepPixel& pixel(int x, int y);
    const DeepPixel& pixel(int x, int y) const;

    /**
     * Alternative accessors using operator()
     */
    DeepPixel& operator()(int x, int y) { return pixel(x, y); }
    const DeepPixel& operator()(int x, int y) const { return pixel(x, y); }

    /**
     * Get total number of samples across all pixels
     */
    size_t totalSampleCount() const;

    /**
     * Get average samples per pixel
     */
    float averageSamplesPerPixel() const;

    /**
     * Get global depth range
     */
    void depthRange(float& minDepth, float& maxDepth) const;

    /**
     * Get the number of non-empty pixels
     */
    size_t nonEmptyPixelCount() const;

    /**
     * Sort all pixels by depth
     */
    void sortAllPixels();

    /**
     * Validate all pixels have correct depth ordering
     */
    bool isValid() const;

    /**
     * Estimate memory usage in bytes
     */
    size_t estimatedMemoryUsage() const;

    /**
     * Clear all pixels
     */
    void clear();

  private:
    int width_;
    int height_;
    std::vector<DeepPixel> pixels_;  // Stored row-major: index = y * width + x

    /**
     * Convert (x, y) to linear index
     */
    size_t index(int x, int y) const;

    /**
     * Check if coordinates are valid
     */
    bool isValidCoord(int x, int y) const;
};

}  // namespace exrio
