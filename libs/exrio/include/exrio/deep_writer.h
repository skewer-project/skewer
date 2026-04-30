#ifndef EXRIO_DEEP_WRITER_H
#define EXRIO_DEEP_WRITER_H

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "deep_image.h"

namespace exrio {

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

/**
 * Streaming deep EXR writer. Lets a producer feed one scanline at a time
 * instead of materializing a whole DeepImage in memory.
 *
 * Usage:
 *   DeepScanlineWriter w(width, height, "/path/to/file.exr");
 *   for (int y = 0; y < height; ++y) {
 *       // Build sample_counts (length width) and a flat samples buffer
 *       // (length sum(sample_counts)) for this row, then:
 *       w.writeScanline(sample_counts, samples);
 *   }
 *
 * The destructor closes the underlying file. If fewer than `height`
 * scanlines are written, the resulting EXR will be incomplete.
 */
class DeepScanlineWriter {
  public:
    DeepScanlineWriter(int width, int height, const std::string& filename);
    ~DeepScanlineWriter();

    DeepScanlineWriter(const DeepScanlineWriter&) = delete;
    DeepScanlineWriter& operator=(const DeepScanlineWriter&) = delete;
    DeepScanlineWriter(DeepScanlineWriter&&) noexcept;
    DeepScanlineWriter& operator=(DeepScanlineWriter&&) noexcept;

    /**
     * Write the next scanline.
     *
     * @param sample_counts Per-pixel sample count, length must equal width().
     * @param samples Concatenated DeepSamples in column order; for pixel x
     *                samples [offset(x), offset(x) + sample_counts[x]).
     */
    void writeScanline(const std::vector<unsigned int>& sample_counts,
                       const std::vector<DeepSample>& samples);

    int width() const;
    int height() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace exrio

#endif  // EXRIO_DEEP_WRITER_H
