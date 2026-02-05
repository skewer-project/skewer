#include "image_io.h"

#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfDeepScanLineOutputFile.h>
#include <ImfHeader.h>
#include <ImfPartType.h>

#include <cassert>
#include <vector>

#include "ImfCompression.h"
#include "ImfMultiPartInputFile.h"
#include "ImfPixelType.h"
#include "film/image_buffer.h"
#include "half.h"

namespace skwr {

#define POINTER_SIZE 4
#define ZBACK 5
#define RED 0
#define GREEN 1
#define BLUE 2

// Helper to compute the base pointer offset for OpenEXR frame buffers
template <typename T>
static char* makeBasePointer(T* data, int minX, int minY, int width) {
    return reinterpret_cast<char*>(data - minX - static_cast<ptrdiff_t>(minY) * width);
    // static_cast<ptrdiff_t> ensures the memory offset calculation happens in 64-bit space,
    // preventing overflow for very large resolution images (may be unecessary but it's a good
    // safeguard)
}

// This is computed at compile-time when possible
static constexpr int getPixelTypeSize(Imf_3_1::PixelType type) {
    switch (type) {
        case Imf_3_1::UINT:
            return sizeof(unsigned int);
        case Imf_3_1::HALF:
            return sizeof(Imath_3_1::half);
        case Imf_3_1::FLOAT:
            return sizeof(float);
        default:
            throw std::runtime_error("Unknown PixelType");
            ;
    }
}

// Helper to insert a deep slice with consistent parameters
static void insertDeepSlice(Imf::DeepFrameBuffer& fb, const char* name, void* ptrs,
                            Imf_3_1::PixelType pixelType, int minX, int minY, int width) {
    // Cast to char** so the pointer arithmetic steps by 'sizeof(char*)' (usually 8 bytes)
    // instead of 1 byte or crashing.
    char** ptrArray = reinterpret_cast<char**>(ptrs);

    fb.insert(name,
              Imf::DeepSlice(pixelType, makeBasePointer(ptrs, minX, minY, width), POINTER_SIZE,
                             POINTER_SIZE * width, getPixelTypeSize(pixelType)));
}

// With custom stride overload
static void insertDeepSlice(Imf::DeepFrameBuffer& fb, const char* name, void* ptrs,
                            Imf_3_1::PixelType pixelType, int minX, int minY, int width, size_t stride) {
    // Cast to char** so the pointer arithmetic steps by 'sizeof(char*)' (usually 8 bytes)
    // instead of 1 byte or crashing.
    char** ptrArray = reinterpret_cast<char**>(ptrs);

    fb.insert(name,
              Imf::DeepSlice(pixelType, makeBasePointer(ptrs, minX, minY, width), POINTER_SIZE,
                             POINTER_SIZE * width, stride));
}

// Check if image exists and is even deep
// NOTE: May not even need this. I believe Imf::DeepScanLineInputFile already checks this
static bool isDeepEXR(const std::string& filename) noexcept {
    try {
        Imf::MultiPartInputFile file(filename.c_str());
        int parts = file.parts();

        // Loop through all parts in the file to check for just one deep part
        for (int i = 0; i < parts; ++i) {
            const Imf::Header& header = file.header(i);
            if (header.hasType() && Imf::isDeepData(header.type())) {
                return true;  // Found a deep part
            }
        }

        return false;
    } catch (...) {
        return false;
    }
}

DeepImageBuffer ImageIO::LoadEXR(const std::string filename) {
    // NOTE: If inputs are TILED, change this to Imf::MultiPartInputFile
    // or Imf::DeepTiledInputFile.
    Imf::DeepScanLineInputFile file(filename.c_str());

    const Imf::Header& header = file.header();

    // Region for which pixel data are available is defined by a second axis-parallel rectangle in
    // pixel space.
    Imath::Box2i dataWindow = header.dataWindow();
    int minX = dataWindow.min.x;
    int minY = dataWindow.min.y;

    /*
     * Defined by the positions of the pixels in the upper left and lower right corners, (x min, y
     * min) and (x max, y max). Imath::Box2i displayWindow = header.displayWindow(); // don't really
     * need it here
     */
    int width = dataWindow.max.x - dataWindow.min.x + 1;
    int height = dataWindow.max.y - dataWindow.min.y + 1;

    const size_t pixelCount = static_cast<size_t>(width) * height;

    std::clog << "    Resolution: " << std::to_string(width) << "x" << std::to_string(height)
              << std::endl;

    // Check for required channels
    Imf::ChannelList channels = header.channels();
    std::vector<ChannelInfo> channelChecks = {
        {"R", true, channels.findChannel("R") != nullptr},
        {"G", true, channels.findChannel("G") != nullptr},
        {"B", true, channels.findChannel("B") != nullptr},
        {"A", true, channels.findChannel("A") != nullptr},
        {"Z", true, channels.findChannel("Z") != nullptr},
        {"ZBack", false, channels.findChannel("ZBack") != nullptr},
    };

    // Check for all required channels
    std::string missing;
    for (const auto& ch : channelChecks) {
        if (ch.required && !ch.present) {
            missing += std::string(ch.name) + ", ";
        }
    }
    if (!missing.empty()) {  // this counts as invalid input data in this file
        throw Iex::InputExc("Missing required channels: " + missing);
    }

    // Check for ZBack property
    const bool hasZBack = channelChecks[ZBACK].present == true;
    if (hasZBack) {
        std::clog << "    Volumetric samples detected (ZBack channel present)" << std::endl;
    }

    /*
     *  Reads only the table of contents for the file. It goes through the file and finds out how
     * many samples exist for every single pixel in the specified row range.
     */
    auto sampleCounts = Imf::Array2D<unsigned int>(height, width);

    // Configure FrameBuffer
    Imf::DeepFrameBuffer frameBuffer;

    // Insert sample count slice to get total Samples
    frameBuffer.insertSampleCountSlice(Imf::Slice(
        Imf_3_1::UINT, (char*)(&sampleCounts[0][0] - dataWindow.min.x - dataWindow.min.y * width),
        sizeof(unsigned int) * 1,        // xStride
        sizeof(unsigned int) * width));  // yStride

    file.setFrameBuffer(frameBuffer);

    file.readPixelSampleCounts(dataWindow.min.y, dataWindow.max.y);

    // Calculate total samples to allocate contiguous memory instead of a pointer for each channel
    // at each pixel
    size_t totalSamples = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            totalSamples += sampleCounts[y][x];
        }
    }

    std::clog << "    Total samples: " << totalSamples << std::endl;

    // Create pointer arrays that OpenEXR expects (one pointer per pixel)
    // These pointers will point into our contiguous vectors.
    Imf::Array2D<float*> rPtrs(height, width);
    Imf::Array2D<float*> gPtrs(height, width);
    Imf::Array2D<float*> bPtrs(height, width);
    Imf::Array2D<float*> aPtrs(height, width);
    Imf::Array2D<float*> zPtrs(height, width);
    Imf::Array2D<float*> zBackPtrs(height, width); // always needed to handle offset logic

    // Automatically populate deepbuf in one take with readPixels
    DeepImageBuffer deepbuf(width, height, totalSamples, sampleCounts);

    // Set up pointers for the deep slices
    size_t offset = 0;  // offset corresponds to the space needed for the number of samples inserted
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned int count = sampleCounts[y][x];

            if (count > 0) {
                // Get pointer to the FIRST sample of this pixel in your final buffer
                // Note: GetMutablePixel returns a view, &view[0] gets the raw DeepSample*
                DeepSample* firstSample = &deepbuf.GetMutablePixel(x, y)[0];

                // --- DANGEROUS POINTER MATH (To access inside Spectrum) ---
                // Since Spectrum uses FLOAT but you want to read HALF, this is risky without casting
                float* rawColor = reinterpret_cast<float*>(&firstSample->color);

                rPtrs[y][x] = &rawColor[RED];
                gPtrs[y][x] = &rawColor[GREEN];
                bPtrs[y][x] = &rawColor[BLUE];

                // Alpha and Z are usually standard types
                aPtrs[y][x] = &firstSample->alpha;
                zPtrs[y][x] = &firstSample->z_front;
                zBackPtrs[y][x] = (float*)&firstSample->z_back; // wire it up and fix later

            } else {
                rPtrs[y][x] = nullptr;
                gPtrs[y][x] = nullptr;
                bPtrs[y][x] = nullptr;
                aPtrs[y][x] = nullptr;
                zPtrs[y][x] = nullptr;
                zBackPtrs[y][x] = nullptr;
            }
        }
    }

    size_t sampleStride = sizeof(DeepSample);

    insertDeepSlice(frameBuffer, "R", &rPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "G", &gPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "B", &bPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "A", &aPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "Z", &zPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    if (hasZBack) {
        insertDeepSlice(frameBuffer, "ZBack", (char**)&zBackPtrs[0][0], Imf_3_1::FLOAT, minX,
                        minY, width, sampleStride);
    }

    file.setFrameBuffer(frameBuffer);  // set updated frame buffer to read pixels

    // Read the pixels, filling each ptr with the corresponding channel info per sample, based on
    // the slices
    file.readPixels(dataWindow.min.y, dataWindow.max.y);

    // Handle missing ZBack channel
    // If the file was hard-surface (no ZBack), we must initialize z_back = z_front manually.
    if (!hasZBack) {
        // We can use the pointers we already set up to do this quickly
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                unsigned int count = sampleCounts[y][x];

                for(unsigned int i = 0; i < count; ++i) {
                     // Array notation works on pointers too
                     zBackPtrs[y][x][i] = zPtrs[y][x][i];
                }
            }
        }
    }

    return deepbuf;
}

// OpenEXR File Layout: https://openexr.com/en/latest/OpenEXRFileLayout.html

void ImageIO::SaveEXR(DeepImageBuffer& buf, const std::string filename) {
    // Box2i takes (min, max). Note the -1 because it is inclusive.
    const int width = buf.GetWidth();
    const int height = buf.GetHeight();

    auto sampleCounts = Imf::Array2D<unsigned int>(height, width);

    // Initialize header information
    Imath::Box2i dataWindow(Imath::V2i(0, 0), Imath::V2i(width - 1, height - 1));
    Imf::Header header(width, height, dataWindow);
    int minX = dataWindow.min.x;
    int minY = dataWindow.min.y;

    header.channels().insert("R", Imf_3_1::Channel(Imf_3_1::FLOAT));
    header.channels().insert("G", Imf_3_1::Channel(Imf_3_1::FLOAT));
    header.channels().insert("B", Imf_3_1::Channel(Imf_3_1::FLOAT));
    header.channels().insert("A", Imf_3_1::Channel(Imf_3_1::FLOAT));
    header.channels().insert("Z", Imf_3_1::Channel(Imf_3_1::FLOAT));
    header.channels().insert("ZBack", Imf_3_1::Channel(Imf_3_1::FLOAT));
    header.setType(Imf_3_1::DEEPSCANLINE);  // NOTE: may change to DEEEPTILE later
    header.compression() =
        Imf_3_1::ZIPS_COMPRESSION;  // what the sample uses. should investigate types further

    Imf_3_1::DeepScanLineOutputFile file(filename.c_str(), header);

    // Create pointer arrays that OpenEXR expects (one pointer per pixel)
    // These pointers will point into our contiguous vectors.
    Imf::Array2D<float*> rPtrs(height, width);
    Imf::Array2D<float*> gPtrs(height, width);
    Imf::Array2D<float*> bPtrs(height, width);
    Imf::Array2D<float*> aPtrs(height, width);
    Imf::Array2D<float*> zPtrs(height, width);
    Imf::Array2D<float*> zBackPtrs(height, width);

    // Set up pointers for the deep slices
    size_t offset = 0;  // offset corresponds to the space needed for the number of samples inserted
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            MutableDeepPixelView pixel = buf.GetMutablePixel(x, y);
            unsigned int count = pixel.count;
            if (count > 0) {
                rPtrs[y][x] = &pixel.data -> color.data()[RED];
                rPtrs[y][x] = &pixel.data -> color.data()[GREEN];
                rPtrs[y][x] = &pixel.data -> color.data()[BLUE];
                aPtrs[y][x] = &pixel.data->alpha;
                zPtrs[y][x] = &pixel.data->z_front;
                zBackPtrs[y][x] = &pixel.data->z_back;

                offset += count;
            } else {
                rPtrs[y][x] = nullptr;
                gPtrs[y][x] = nullptr;
                bPtrs[y][x] = nullptr;
                aPtrs[y][x] = nullptr;
                zPtrs[y][x] = nullptr;
                zBackPtrs[y][x] = nullptr;
            }
        }
    }

    // Create and populate frameBuffer
    Imf_3_1::DeepFrameBuffer frameBuffer;

    // Insert sample count slice (still needed for readPixels validation internally by OpenEXR)
    frameBuffer.insertSampleCountSlice(Imf::Slice(
        Imf_3_1::UINT, (char*)(&sampleCounts[0][0] - dataWindow.min.x - dataWindow.min.y * width),
        sizeof(unsigned int) * 1,        // xStride
        sizeof(unsigned int) * width));  // yStride

    size_t sampleStride = sizeof(DeepSample);

    insertDeepSlice(frameBuffer, "R", &rPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "G", &gPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "B", &bPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "A", &aPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "Z", &zPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);
    insertDeepSlice(frameBuffer, "ZBack", &zBackPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width, sampleStride);

    file.setFrameBuffer(frameBuffer);

   file.writePixels(height);
}
