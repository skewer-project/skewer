#pragma once

#include <string>

#include "deep_image.h"

namespace deep_compositor {

/**
 * Exception thrown for deep EXR reading errors
 */
class DeepReaderException : public std::runtime_error {
  public:
    explicit DeepReaderException(const std::string& message) : std::runtime_error(message) {}
};

/**
 * Load a deep OpenEXR file into a DeepImage
 *
 * @param filename Path to the deep EXR file
 * @return Loaded DeepImage with all samples
 * @throws DeepReaderException on file errors
 */
DeepImage loadDeepEXR(const std::string& filename);

/**
 * Check if a file is a valid deep EXR file
 *
 * @param filename Path to the file
 * @return true if it's a valid deep EXR
 */
bool isDeepEXR(const std::string& filename);

/**
 * Get information about a deep EXR file without fully loading it
 *
 * @param filename Path to the deep EXR file
 * @param width Output: image width
 * @param height Output: image height
 * @param isDeep Output: true if deep image
 * @return true if file info was read successfully
 */
bool getDeepEXRInfo(const std::string& filename, int& width, int& height, bool& isDeep);

}  // namespace deep_compositor
