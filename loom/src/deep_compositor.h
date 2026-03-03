#pragma once

#include <exrio/deep_image.h>

#include <vector>

namespace exrio {

/**
 * Options for the compositing operation
 */
struct CompositorOptions {
    float mergeThreshold = 0.001f;  // Epsilon for merging nearby samples
    bool enableMerging = true;      // Whether to merge nearby samples
};

/**
 * Statistics from a compositing operation
 */
struct CompositorStats {
    size_t inputImageCount = 0;
    size_t totalInputSamples = 0;
    size_t totalOutputSamples = 0;
    float minDepth = 0.0f;
    float maxDepth = 0.0f;
    double mergeTimeMs = 0.0;
    double flattenTimeMs = 0.0;
};

/**
 * Deep merge multiple deep images into a single deep image
 *
 * Combines all samples from all input images, sorting by depth.
 * All input images must have the same dimensions.
 *
 * @param inputs Vector of deep images to merge
 * @param options Compositing options
 * @param stats Optional output statistics
 * @return Merged deep image
 * @throws std::runtime_error if inputs have mismatched dimensions
 */
DeepImage deepMerge(const std::vector<DeepImage>& inputs,
                    const CompositorOptions& options = CompositorOptions(),
                    CompositorStats* stats = nullptr);

/**
 * Deep merge (pointer version for large images)
 */
DeepImage deepMerge(const std::vector<const DeepImage*>& inputs,
                    const CompositorOptions& options = CompositorOptions(),
                    CompositorStats* stats = nullptr);

/**
 * Merge samples from multiple deep pixels into one
 *
 * @param pixels Vector of deep pixels to merge
 * @param mergeThreshold Epsilon for merging nearby samples
 * @return Merged deep pixel with sorted samples
 */
DeepPixel mergePixels(const std::vector<const DeepPixel*>& pixels, float mergeThreshold = 0.001f);

/**
 * Validate that all images have compatible dimensions
 *
 * @param inputs Vector of images to validate
 * @return true if all dimensions match
 */
bool validateDimensions(const std::vector<DeepImage>& inputs);
bool validateDimensions(const std::vector<const DeepImage*>& inputs);

}  // namespace exrio
