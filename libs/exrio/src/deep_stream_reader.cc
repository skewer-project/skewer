#include "exrio/deep_stream_reader.h"

#include <stdexcept>

namespace exrio {

DeepStreamReader::DeepStreamReader(const std::string& filename)
    : file_(filename.c_str())  // This opens the file immediately
{
    // Once the file is open, we extract the metadata (width/height) from the header
    Imath::Box2i dw = file_.header().dataWindow();
    min_x_ = dw.min.x;
    min_y_ = dw.min.y;
    width_ = dw.max.x - dw.min.x + 1;
    height_ = dw.max.y - dw.min.y + 1;

    // Note: If DeepScanLineInputFile opened successfully, it's guaranteed to be a deep file.
    // If it wasn't deep, opening it would throw an exception above.
}

const unsigned int* DeepStreamReader::getSampleCountsForRow(int y) {
    FetchSampleCounts(y);
    return temp_sample_counts.data();  // return pointer to the start of the row's sample counts
}

void DeepStreamReader::FetchSampleCounts(int y) {
    // Resize buffer to fit one row of integers
    temp_sample_counts.resize(width_);

    Imf::DeepFrameBuffer countBuffer;
    // We point to the start of our vector, but tell OpenEXR
    // that this memory represents pixel (min_x_, y)
    char* base = (char*)(temp_sample_counts.data()) - (min_x_ * sizeof(unsigned int));
    // Note: We don't subtract y because we only read one row (y, y)

    countBuffer.insertSampleCountSlice(Imf::Slice(Imf::UINT, base,
                                                    sizeof(unsigned int),  // xStride
                                                    0  // yStride (0 because we read 1 row)
                                                    ));

    file_.setFrameBuffer(countBuffer);
    int exr_y = y + min_y_;
    file_.readPixelSampleCounts(exr_y, exr_y);
}

}  // namespace exrio
