#include "image_io.h"

#include <ImathBox.h>
#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfPixelType.h>

#include <cassert>
#include <cstddef>

#include "film/image_buffer.h"
#include "stb_image.h"
#include "stb_image_write.h"

namespace skwr {

// =============================================================================================
// Helper Functions (OpenEXR)
// =============================================================================================

// Helper to compute the base pointer offset for OpenEXR frame buffers
template <typename T>
static auto MakeBasePointer(T* data, int min_x, int min_y, [[maybe_unused]] int width,
                            size_t x_stride, size_t y_stride) -> char* {
    return reinterpret_cast<char*>(data) - (static_cast<ptrdiff_t>(min_x) * x_stride) -
           (static_cast<ptrdiff_t>(min_y) * y_stride);
}

// With custom stride overload
static void InsertDeepSlice(Imf::DeepFrameBuffer& fb, const char* name, void* ptrs,
                            Imf::PixelType pixel_type, int min_x, int min_y, int width,
                            size_t sample_stride) {
    size_t const x_stride = sizeof(float*);
    size_t const y_stride = x_stride * width;

    fb.insert(name, Imf::DeepSlice(pixel_type,
                                   makeBasePointer(ptrs, min_x, min_y, width, x_stride, y_stride),
                                   x_stride, y_stride, sample_stride));
}

// =============================================================================================
// PPM / Flat Image I/O
// =============================================================================================

// =============================================================================================
// Deep Image I/O (OpenEXR)
// =============================================================================================

static auto ImageIO::LoadEXR(const std::string& filename) -> DeepImageBuffer {
    Imf::DeepScanLineInputFile file(filename.c_str());
    const Imf::Header& header = file.header();

    Imath::Box2i data_window = header.dataWindow();
    int min_x = dataWindow.min.x;
    int min_y = dataWindow.min.y;
    int width = dataWindow.max.x - dataWindow.min.x + 1;
    int height = dataWindow.max.y - dataWindow.min.y + 1;

    // Check channels
    Imf::ChannelList channels = header.channels();
    const bool kHasZBack = channels.findChannel("ZBack") != nullptr;

    // Prepare all pointer arrays (even if we don't have data yet)
    auto sample_counts = Imf::Array2D<unsigned int>(height, width);
    Imf::Array2D<const float*> r_ptrs(height, width);
    Imf::Array2D<const float*> g_ptrs(height, width);
    Imf::Array2D<const float*> b_ptrs(height, width);
    Imf::Array2D<const float*> a_ptrs(height, width);
    Imf::Array2D<const float*> z_ptrs(height, width);
    Imf::Array2D<const float*> z_back_ptrs(height, width);

    // Configure FrameBuffer with everything
    Imf::DeepFrameBuffer frame_buffer;

    size_t count_x_stride = sizeof(unsigned int);
    size_t count_y_stride = count_x_stride * width;

    frame_buffer.insertSampleCountSlice(Imf::Slice(
        Imf::UINT,
        makeBasePointer(&sampleCounts[0][0], min_x, min_y, width, count_x_stride, count_y_stride),
        count_x_stride, count_y_stride));

    size_t sample_stride = sizeof(DeepSample);

    insertDeepSlice(frame_buffer, "R", &rPtrs[0][0], Imf::FLOAT, min_x, min_y, width,
                    sample_stride);
    insertDeepSlice(frame_buffer, "G", &gPtrs[0][0], Imf::FLOAT, min_x, min_y, width,
                    sample_stride);
    insertDeepSlice(frame_buffer, "B", &bPtrs[0][0], Imf::FLOAT, min_x, min_y, width,
                    sample_stride);
    insertDeepSlice(frame_buffer, "A", &aPtrs[0][0], Imf::FLOAT, min_x, min_y, width,
                    sample_stride);
    insertDeepSlice(frame_buffer, "Z", &zPtrs[0][0], Imf::FLOAT, min_x, min_y, width,
                    sample_stride);

    if (kHasZBack) {
        insertDeepSlice(frame_buffer, "ZBack", &zBackPtrs[0][0], Imf::FLOAT, min_x, min_y, width,
                        sample_stride);
    }

    file.setFrameBuffer(frame_buffer);

    // Read Sample Counts
    file.readPixelSampleCounts(dataWindow.min.y, dataWindow.max.y);

    // Calculate total samples and allocate
    size_t total_samples = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            total_samples += sampleCounts[y][x];
        }
    }

    DeepImageBuffer deepbuf(width, height, total_samples, sampleCounts);

    // Now populate the pointers in our arrays
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned int count = sampleCounts[y][x];
            if (count > 0) {
                // Grab the address information of the sample at the start of the pixel
                size_t start = deepbuf.pixelOffsets_[(y * width) + x];
                DeepSample& first_sample = deepbuf.allSamples_[start];

                r_ptrs[y][x] = &first_sample.r;
                g_ptrs[y][x] = &first_sample.g;
                b_ptrs[y][x] = &first_sample.b;
                a_ptrs[y][x] = &first_sample.alpha;
                z_ptrs[y][x] = &first_sample.z_front;

                if (kHasZBack) {
                    z_back_ptrs[y][x] = &first_sample.z_back;
                } else {
                    z_back_ptrs[y][x] = nullptr;
                }
            } else {
                r_ptrs[y][x] = nullptr;
                g_ptrs[y][x] = nullptr;
                b_ptrs[y][x] = nullptr;
                a_ptrs[y][x] = nullptr;
                z_ptrs[y][x] = nullptr;
                z_back_ptrs[y][x] = nullptr;
            }
        }
    }

    // Read Pixels (pointers in rPtrs etc. are now valid)
    file.readPixels(dataWindow.min.y, dataWindow.max.y);

    // Handle missing ZBack channel manually
    if (!kHasZBack) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t start = deepbuf.pixelOffsets_[(y * width) + x];
                size_t end = deepbuf.pixelOffsets_[(y * width) + x + 1];
                size_t const count = end - start;

                for (size_t i = 0; i < count; ++i) {
                    deepbuf.allSamples_[start + i].z_back = deepbuf.allSamples_[start + i].z_front;
                }
            }
        }
    }

    return deepbuf;
}

}  // namespace skwr
