#ifndef EXRIO_DEEP_STREAM_READER_H
#define EXRIO_DEEP_STREAM_READER_H

#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfMultiPartInputFile.h>

#include <memory>
#include <vector>

namespace exrio {

/**
 * Streaming reader for deep OpenEXR files
 *
 * Provides row-by-row access to deep EXR data without loading the entire image into memory.
 * Uses OpenEXR's DeepScanLineInputFile for efficient sequential reading.
 *
 * Typical usage pattern:
 * @code
 *   DeepStreamReader reader("input.exr");
 *   int height = reader.getHeight();
 *   int width = reader.getWidth();
 *
 *   for (int y = 0; y < height; ++y) {
 *       const unsigned int* counts = reader.getSampleCountsForRow(y);
 *       // Use OpenEXR's DeepFrameBuffer API to read actual pixel data
 *       Imf::DeepFrameBuffer fb;
 *       // ... setup frame buffer with allocated data ...
 *       reader.getNativeHandle().setFrameBuffer(fb);
 *       reader.getNativeHandle().readPixels(y, y);
 *   }
 * @endcode
 *
 * The reader maintains a file handle and reusable temporary buffer for sample counts.
 * This is memory-efficient for processing large images in windows/chunks.
 */
class DeepStreamReader {
  public:
    /**
     * Constructor: Opens the file and extracts metadata from the header
     *
     * @param filename Path to the deep EXR file to open
     * @throws std::runtime_error if file cannot be opened or is not a valid deep EXR
     *
     * This constructor performs eager initialization:
     * - Opens the file immediately
     * - Reads header and extracts data window bounds (min_x, min_y, width, height)
     */
    explicit DeepStreamReader(const std::string& filename);

    /**
     * Image dimensions
     * @return Width of the deep EXR image
     */
    int getWidth() const { return width_; }

    /**
     * Image dimensions
     * @return Height of the deep EXR image
     */
    int getHeight() const { return height_; }

    /**
     * Data window offset in X
     * @return Minimum X coordinate of the data window (typically 0)
     */
    int getMinX() const { return min_x_; }

    /**
     * Data window offset in Y
     * @return Minimum Y coordinate of the data window (typically 0)
     */
    int getMinY() const { return min_y_; }

    /**
     * Get sample counts for a single row
     *
     * Fetches the number of samples for each pixel in row y.
     * The returned pointer points to a temporary buffer that is valid until
     * the next call to getSampleCountsForRow().
     *
     * @param y Row index (0 to height-1)
     * @return Pointer to array of sample counts (width elements)
     *
     * This method uses OpenEXR's readPixelSampleCounts() to read only metadata,
     * not the actual sample data. The caller is responsible for reading pixel
     * data separately via the native file handle.
     */
    const unsigned int* getSampleCountsForRow(int y);

    /**
     * Get direct access to the underlying OpenEXR file handle
     *
     * Advanced users can use this for direct OpenEXR API calls:
     * - setFrameBuffer() to configure sampling layout
     * - readPixels() to read pixel data
     * - readPixelSampleCounts() (also exposed through getSampleCountsForRow())
     *
     * @return Reference to the DeepScanLineInputFile
     *
     * Note: Care must be taken when using the native handle directly.
     * The file handle is owned by this reader and must not be stored
     * after the reader is destroyed.
     */
    Imf::DeepScanLineInputFile& getNativeHandle() { return file_; }

    // Non-copyable: File handles cannot be duplicated safely
    DeepStreamReader(const DeepStreamReader&) = delete;
    DeepStreamReader& operator=(const DeepStreamReader&) = delete;

    virtual ~DeepStreamReader() = default;

  private:
    /**
     * Fetch sample counts for a specific row
     *
     * This function uses the OpenEXR IMF library to efficiently load deep image metadata:
     * - Resizes the temporary buffer to accommodate one row of sample count integers
     * - Calculates the byte offset corresponding to the requested row
     * - Configures a DeepFrameBuffer with a sample count slice pointing to the appropriate
     *   memory location using pointer arithmetic
     * - Reads only the sample counts (not the actual sample data) for the specified row
     *
     * The pointer arithmetic adjusts the base memory address by the total offset so that
     * the IMF library can correctly map pixel coordinates to buffer locations:
     *    base = temp_sample_counts.data() - (min_x_ * sizeof(unsigned int))
     * This allows OpenEXR to write pixel (min_x_, y) to the correct location in our buffer.
     *
     * @param y Row index to fetch sample counts for
     */
    void FetchSampleCounts(int y);

    // Image dimensions from data window
    int width_;
    int height_;
    int min_x_;
    int min_y_;

    // Temporary buffer for sample counts of a single row (reused across calls)
    std::vector<unsigned int> temp_sample_counts;

    // The open file handle (kept open for the lifetime of this reader)
    Imf::DeepScanLineInputFile file_;

    // Coordinate validation helper (for future use)
    bool IsValidCoord(int x, int y) const {
        return (x >= 0 && x < width_ && y >= 0 && y < height_);
    }

    // Helper to convert (x, y) to linear index (for future use)
    size_t index(int x, int y) const { return static_cast<size_t>(y) * width_ + x; }
};

}  // namespace exrio

#endif  // EXRIO_DEEP_STREAM_READER_H
