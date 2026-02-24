#include <exrio/deep_image.h>
#include <sstream>

namespace deep_compositor {

// ============================================================================
// DeepPixel Implementation
// ============================================================================

void DeepPixel::addSample(const DeepSample& sample) {
    // Insert in sorted order (front to back by depth)
    auto it = std::lower_bound(samples_.begin(), samples_.end(), sample);
    samples_.insert(it, sample);
}

void DeepPixel::addSamples(const std::vector<DeepSample>& newSamples) {
    samples_.reserve(samples_.size() + newSamples.size());
    for (const auto& sample : newSamples) {
        samples_.push_back(sample);
    }
    sortByDepth();
}

void DeepPixel::sortByDepth() {
    std::sort(samples_.begin(), samples_.end());
}

void DeepPixel::mergeSamplesWithinEpsilon(float epsilon) {
    if (samples_.size() < 2) {
        return;
    }
    
    std::vector<DeepSample> merged;
    merged.reserve(samples_.size());
    
    size_t i = 0;
    while (i < samples_.size()) {
        // Start a new merged sample
        DeepSample current = samples_[i];
        float totalAlpha = current.alpha;
        float weightedR = current.red;
        float weightedG = current.green;
        float weightedB = current.blue;
        float avgDepth = current.depth;
        float avgDepthBack = current.depth_back;
        int count = 1;
        
        // Merge with subsequent samples within epsilon
        while (i + 1 < samples_.size() &&
               samples_[i + 1].depth - current.depth < epsilon &&
               std::abs(samples_[i + 1].depth_back - current.depth_back) < epsilon) {
            i++;
            const DeepSample& next = samples_[i];
            
            // Accumulate for averaging
            totalAlpha += next.alpha;
            weightedR += next.red;
            weightedG += next.green;
            weightedB += next.blue;
            avgDepth += next.depth;
            avgDepthBack += next.depth_back;
            count++;
        }
        
        // Create merged sample
        if (count > 1) {
            current.depth = avgDepth / count;
            current.depth_back = avgDepthBack / count;
            current.red = weightedR / count;
            current.green = weightedG / count;
            current.blue = weightedB / count;
            current.alpha = std::min(1.0f, totalAlpha / count);
        }
        
        merged.push_back(current);
        i++;
    }
    
    samples_ = std::move(merged);
}

float DeepPixel::minDepth() const {
    if (samples_.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    return samples_.front().depth;  // Already sorted front-to-back
}

float DeepPixel::maxDepth() const {
    if (samples_.empty()) {
        return -std::numeric_limits<float>::infinity();
    }
    // Return the farthest depth_back across all samples
    float maxVal = -std::numeric_limits<float>::infinity();
    for (const auto& s : samples_) {
        maxVal = std::max(maxVal, s.depth_back);
    }
    return maxVal;
}

bool DeepPixel::isValidSortOrder() const {
    for (size_t i = 1; i < samples_.size(); ++i) {
        if (samples_[i].depth < samples_[i - 1].depth) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// DeepImage Implementation
// ============================================================================

DeepImage::DeepImage() : width_(0), height_(0) {}

DeepImage::DeepImage(int width, int height) : width_(0), height_(0) {
    resize(width, height);
}

void DeepImage::resize(int width, int height) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("Image dimensions must be non-negative");
    }
    
    width_ = width;
    height_ = height;
    pixels_.clear();
    pixels_.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
}

size_t DeepImage::index(int x, int y) const {
    return static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x);
}

bool DeepImage::isValidCoord(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

DeepPixel& DeepImage::pixel(int x, int y) {
    if (!isValidCoord(x, y)) {
        throw std::out_of_range("Pixel coordinates out of range");
    }
    return pixels_[index(x, y)];
}

const DeepPixel& DeepImage::pixel(int x, int y) const {
    if (!isValidCoord(x, y)) {
        throw std::out_of_range("Pixel coordinates out of range");
    }
    return pixels_[index(x, y)];
}

size_t DeepImage::totalSampleCount() const {
    size_t total = 0;
    for (const auto& pixel : pixels_) {
        total += pixel.sampleCount();
    }
    return total;
}

float DeepImage::averageSamplesPerPixel() const {
    if (pixels_.empty()) {
        return 0.0f;
    }
    return static_cast<float>(totalSampleCount()) / static_cast<float>(pixels_.size());
}

void DeepImage::depthRange(float& minDepth, float& maxDepth) const {
    minDepth = std::numeric_limits<float>::infinity();
    maxDepth = -std::numeric_limits<float>::infinity();
    
    for (const auto& pixel : pixels_) {
        if (!pixel.isEmpty()) {
            minDepth = std::min(minDepth, pixel.minDepth());
            maxDepth = std::max(maxDepth, pixel.maxDepth());
        }
    }
}

size_t DeepImage::nonEmptyPixelCount() const {
    size_t count = 0;
    for (const auto& pixel : pixels_) {
        if (!pixel.isEmpty()) {
            count++;
        }
    }
    return count;
}

void DeepImage::sortAllPixels() {
    for (auto& pixel : pixels_) {
        pixel.sortByDepth();
    }
}

bool DeepImage::isValid() const {
    for (const auto& pixel : pixels_) {
        if (!pixel.isValidSortOrder()) {
            return false;
        }
    }
    return true;
}

size_t DeepImage::estimatedMemoryUsage() const {
    // Base structure size
    size_t usage = sizeof(DeepImage);
    
    // Vector overhead
    usage += pixels_.capacity() * sizeof(DeepPixel);
    
    // Sample data
    for (const auto& pixel : pixels_) {
        usage += pixel.samples().capacity() * sizeof(DeepSample);
    }
    
    return usage;
}

void DeepImage::clear() {
    for (auto& pixel : pixels_) {
        pixel.clear();
    }
}

} // namespace deep_compositor
