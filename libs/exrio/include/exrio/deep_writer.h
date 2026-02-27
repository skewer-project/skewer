#pragma once

#include <array>
#include <string>

#include "deep_image.h"

namespace deep_compositor {

/**
 * Exception thrown for deep EXR writing errors
 */
class DeepWriterException : public std::runtime_error {
  public:
    explicit DeepWriterException(const std::string& message) : std::runtime_error(message) {}
};

/**
 * Write a deep image to an OpenEXR file
 *
 * @param img The deep image to write
 * @param filename Output path
 * @throws DeepWriterException on file errors
 */
void writeDeepEXR(const DeepImage& img, const std::string& filename);

/**
 * Write a flattened version of a deep image to a standard EXR file
 *
 * @param img The deep image to flatten and write
 * @param filename Output path
 * @throws DeepWriterException on file errors
 */
void writeFlatEXR(const DeepImage& img, const std::string& filename);

/**
 * Write a pre-flattened RGBA buffer to a standard EXR file
 *
 * @param rgba Flattened RGBA data (width * height * 4 floats)
 * @param width Image width
 * @param height Image height
 * @param filename Output path
 */
void writeFlatEXR(const std::vector<float>& rgba, int width, int height,
                  const std::string& filename);

/**
 * Write a flattened, tone-mapped PNG image
 *
 * @param img The deep image to flatten and write
 * @param filename Output path
 * @throws DeepWriterException on file errors or if PNG support not compiled in
 */
void writePNG(const DeepImage& img, const std::string& filename);

/**
 * Write a pre-flattened RGBA buffer to PNG
 *
 * @param rgba Flattened RGBA data (width * height * 4 floats)
 * @param width Image width
 * @param height Image height
 * @param filename Output path
 */
void writePNG(const std::vector<float>& rgba, int width, int height, const std::string& filename);

/**
 * Check if PNG support is available
 */
bool hasPNGSupport();

/**
 * Flatten a deep pixel using front-to-back over operation
 * Returns [R, G, B, A]
 */
std::array<float, 4> flattenPixel(const DeepPixel& pixel);

/**
 * Flatten an entire deep image to RGBA buffer
 * Returns buffer of width * height * 4 floats
 */
std::vector<float> flattenImage(const DeepImage& img);

}  // namespace deep_compositor
