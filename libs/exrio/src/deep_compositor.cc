#include <exrio/deep_compositor.h>
#include <exrio/deep_volume.h>
#include <exrio/utils.h>

#include <algorithm>
#include <stdexcept>

namespace deep_compositor {

bool validateDimensions(const std::vector<DeepImage>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    
    int width = inputs[0].width();
    int height = inputs[0].height();
    
    for (size_t i = 1; i < inputs.size(); ++i) {
        if (inputs[i].width() != width || inputs[i].height() != height) {
            return false;
        }
    }
    
    return true;
}

bool validateDimensions(const std::vector<const DeepImage*>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    
    int width = inputs[0]->width();
    int height = inputs[0]->height();
    
    for (size_t i = 1; i < inputs.size(); ++i) {
        if (inputs[i]->width() != width || inputs[i]->height() != height) {
            return false;
        }
    }
    
    return true;
}

DeepPixel mergePixels(const std::vector<const DeepPixel*>& pixels,
                      float mergeThreshold) {
    return mergePixelsVolumetric(pixels, mergeThreshold);
}

DeepImage deepMerge(const std::vector<DeepImage>& inputs,
                    const CompositorOptions& options,
                    CompositorStats* stats) {
    // Convert to pointer version
    std::vector<const DeepImage*> ptrs;
    ptrs.reserve(inputs.size());
    for (const auto& img : inputs) {
        ptrs.push_back(&img);
    }
    
    return deepMerge(ptrs, options, stats);
}

DeepImage deepMerge(const std::vector<const DeepImage*>& inputs,
                    const CompositorOptions& options,
                    CompositorStats* stats) {
    Timer timer;
    
    // Handle empty input
    if (inputs.empty()) {
        if (stats) {
            stats->inputImageCount = 0;
        }
        return DeepImage();
    }
    
    // Validate dimensions
    if (!validateDimensions(inputs)) {
        throw std::runtime_error("Input images have mismatched dimensions");
    }
    
    int width = inputs[0]->width();
    int height = inputs[0]->height();
    
    // Calculate input statistics
    size_t totalInputSamples = 0;
    float minDepth = std::numeric_limits<float>::infinity();
    float maxDepth = -std::numeric_limits<float>::infinity();
    
    for (const auto* img : inputs) {
        totalInputSamples += img->totalSampleCount();
        
        float imgMin, imgMax;
        img->depthRange(imgMin, imgMax);
        minDepth = std::min(minDepth, imgMin);
        maxDepth = std::max(maxDepth, imgMax);
    }
    
    logVerbose("  Merging " + std::to_string(inputs.size()) + " images...");
    logVerbose("    Input samples: " + formatNumber(totalInputSamples));
    
    // Create output image
    DeepImage result(width, height);
    
    // Prepare pixel pointer arrays for each input
    std::vector<const DeepPixel*> pixelPtrs(inputs.size());
    
    // Merge each pixel
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Gather pixel pointers from all inputs
            for (size_t i = 0; i < inputs.size(); ++i) {
                pixelPtrs[i] = &(inputs[i]->pixel(x, y));
            }
            
            // Merge pixels
            float threshold = options.enableMerging ? options.mergeThreshold : 0.0f;
            result.pixel(x, y) = mergePixels(pixelPtrs, threshold);
        }
    }
    
    double mergeTime = timer.elapsedMs();
    
    // Calculate output statistics
    size_t totalOutputSamples = result.totalSampleCount();
    
    logVerbose("    Output samples: " + formatNumber(totalOutputSamples));
    logVerbose("    Depth range: " + std::to_string(minDepth) + " to " + std::to_string(maxDepth));
    logVerbose("    Merge time: " + std::to_string(static_cast<int>(mergeTime)) + " ms");
    
    // Fill stats if requested
    if (stats) {
        stats->inputImageCount = inputs.size();
        stats->totalInputSamples = totalInputSamples;
        stats->totalOutputSamples = totalOutputSamples;
        stats->minDepth = minDepth;
        stats->maxDepth = maxDepth;
        stats->mergeTimeMs = mergeTime;
    }
    
    return result;
}

} // namespace deep_compositor
