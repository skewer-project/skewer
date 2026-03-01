#pragma once
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h> // For reading deep EXR files
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>


namespace deep_compositor {

    
class DeepInfo {
public:   
    DeepInfo();
    DeepInfo(const std::string& filename) 
        : file_(filename.c_str()) // This opens the file immediately
    {
        // Once the file is open, we extract the metadata (width/height)
        Imath::Box2i dw = file_.header().dataWindow();
        width_  = dw.max.x - dw.min.x + 1;
        height_ = dw.max.y - dw.min.y + 1;
        printf("Loaded Deep EXR: %s (%dx%d)\n", filename.c_str(), width_, height_);
        printf("Number of parts in file: %d\n", file_.header().hasType());
        // We can verify if it's deep, though DeepScanLineInputFile 
        // will throw an error if you point it at a non-deep file anyway.
    }

    int width() const { return width_; }
    int height() const { return height_; }
    bool isDeep() const { return isDeep_; }

    Imf::DeepScanLineInputFile& getFile()  { return file_; }

    // Temporary buffer for sample counts of a single row
    const unsigned int* getSampleCountsForRow(int y) {
        fetchSampleCounts(y);
        return tempSampleCounts.data();  // return pointer to the start of the row's sample counts
    }

    /**
     * This function uses the OpenEXR IMF library to efficiently load deep image data:
     * - Resizes the temporary buffer to accommodate one row of sample count integers
     * - Calculates the byte offset corresponding to the requested row
     * - Configures a DeepFrameBuffer with a sample count slice pointing to the appropriate
     *   memory location using pointer arithmetic
     * - Reads only the sample counts (not the actual sample data) for the specified row
     * 
     * The pointer arithmetic adjusts the base memory address by the total offset so that
     * the IMF library can correctly map pixel coordinates to buffer locations.
     * 
 
     */
    void fetchSampleCounts(int y) {
        // Resize buffer to fit one row of integers
        tempSampleCounts.resize(width_);

        Imath::Box2i dw = file_.header().dataWindow();
        int minX = dw.min.x;

        Imf::DeepFrameBuffer countBuffer;
        // We point to the start of our vector, but tell OpenEXR 
        // that this memory represents pixel (minX, y)
        char* base = (char*)(tempSampleCounts.data()) 
                    - (minX * sizeof(unsigned int)); 
                    // Note: We don't subtract y because we only read one row (y, y)

        countBuffer.insertSampleCountSlice(Imf::Slice(
            Imf::UINT, 
            base, 
            sizeof(unsigned int), // xStride
            0                     // yStride (0 because we read 1 row)
        ));

        file_.setFrameBuffer(countBuffer);
        file_.readPixelSampleCounts(y, y);
    }


    DeepInfo(DeepInfo&&) noexcept = default;
    DeepInfo& operator=(DeepInfo&&) noexcept = default;

    // 2. Explicitly forbid Copying (Since the EXR file handle can't be duplicated)
    DeepInfo(const DeepInfo&) = delete;
    DeepInfo& operator=(const DeepInfo&) = delete;

    // 3. Virtual destructor is good practice if you ever plan to inherit
    virtual ~DeepInfo() = default;


private:
    int width_;
    int height_;

    std::vector<unsigned int> tempSampleCounts;
    
    Imf::DeepScanLineInputFile file_;

    bool isDeep_;
    bool isValidCoord(int x, int y) const {
        return (x >= 0 && x < width_ && y >= 0 && y < height_);
    }

    // Helper to convert (x, y) to linear index for any internal arrays
    size_t index(int x, int y) const { return static_cast<size_t>(y) * width_ + x; }
};


}