#pragma once

#include <exrio/deep_image.h>

#include <string>
#include <vector>

namespace exrio {

// Phase 1: Read files from disk into memory.
// Throws std::runtime_error on failure.
std::vector<DeepImage> LoadImagesPhase(const std::vector<std::string>& inputFiles);

// Phase 2: Merge the deep images into a single image.
// See deepMerge() in deep_compositor.cc

// Phase 3: Flatten the deep image into a standard 2D image.
std::vector<float> FlattenPhase(const DeepImage& mergedImage);

// Phase 4: Write the results back to disk.
// Throws std::runtime_error on failure.
void WriteOutputsPhase(const DeepImage& mergedImage, const std::vector<float>& flatRgba,
                       const std::string& outputPrefix, bool deepOutput, bool flatOutput,
                       bool pngOutput);

}  // namespace exrio
