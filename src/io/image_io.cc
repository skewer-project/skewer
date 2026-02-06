#include "image_io.h"

#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfDeepScanLineOutputFile.h>
#include <ImfHeader.h>
#include <ImfMultiPartInputFile.h>
#include <ImfPartType.h>
#include <ImfPixelType.h>
#include <half.h>

#include <cassert>
#include <iostream>

#include "film/image_buffer.h"

namespace skwr {

#define RED 0
#define GREEN 1
#define BLUE 2

// =============================================================================================
// Helper Functions (OpenEXR)
// =============================================================================================

// Helper to compute the base pointer offset for OpenEXR frame buffers
template <typename T>
static char* makeBasePointer(T* data, int minX, int minY, int width, size_t xStride,
                             size_t yStride) {
    return reinterpret_cast<char*>(data) - static_cast<ptrdiff_t>(minX) * xStride -
           static_cast<ptrdiff_t>(minY) * yStride;
}

// This is computed at compile-time when possible
static constexpr int getPixelTypeSize(Imf_3_2::PixelType type) {
    switch (type) {
        case Imf_3_2::UINT:
            return sizeof(unsigned int);
        case Imf_3_2::HALF:
            return sizeof(Imath_3_1::half);
        case Imf_3_2::FLOAT:
            return sizeof(float);
        default:
            throw std::runtime_error("Unknown PixelType");
    }
}

// With custom stride overload
static void insertDeepSlice(Imf::DeepFrameBuffer& fb, const char* name, void* ptrs,
                            Imf_3_2::PixelType pixelType, int minX, int minY, int width,
                            size_t sampleStride) {
    size_t xStride = sizeof(float*);
    size_t yStride = xStride * width;

    fb.insert(name,
              Imf::DeepSlice(pixelType, makeBasePointer(ptrs, minX, minY, width, xStride, yStride),
                             xStride, yStride, sampleStride));
}

// =============================================================================================
// PPM / Flat Image I/O
// =============================================================================================

// =============================================================================================
// Deep Image I/O (OpenEXR)
// =============================================================================================

DeepImageBuffer ImageIO::LoadEXR(const std::string filename) {
    Imf::DeepScanLineInputFile file(filename.c_str());
    const Imf::Header& header = file.header();

    Imath::Box2i dataWindow = header.dataWindow();
    int minX = dataWindow.min.x;
    int minY = dataWindow.min.y;
    int width = dataWindow.max.x - dataWindow.min.x + 1;
    int height = dataWindow.max.y - dataWindow.min.y + 1;

    std::clog << "Loading EXR: " << filename << " (" << width << "x" << height << ")" << std::endl;

    // Check channels
    Imf::ChannelList channels = header.channels();
    const bool hasZBack = channels.findChannel("ZBack") != nullptr;

    // Prepare all pointer arrays (even if we don't have data yet)
    auto sampleCounts = Imf::Array2D<unsigned int>(height, width);
    Imf::Array2D<float*> rPtrs(height, width);
    Imf::Array2D<float*> gPtrs(height, width);
    Imf::Array2D<float*> bPtrs(height, width);
    Imf::Array2D<float*> aPtrs(height, width);
    Imf::Array2D<float*> zPtrs(height, width);
    Imf::Array2D<float*> zBackPtrs(height, width);

    // Configure FrameBuffer with everything
    Imf::DeepFrameBuffer frameBuffer;

    size_t countXStride = sizeof(unsigned int);
    size_t countYStride = countXStride * width;

    frameBuffer.insertSampleCountSlice(Imf::Slice(
        Imf_3_2::UINT,
        makeBasePointer(&sampleCounts[0][0], minX, minY, width, countXStride, countYStride),
        countXStride, countYStride));

    size_t sampleStride = sizeof(DeepSample);

    insertDeepSlice(frameBuffer, "R", &rPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "G", &gPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "B", &bPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "A", &aPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "Z", &zPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);

    if (hasZBack) {
        insertDeepSlice(frameBuffer, "ZBack", &zBackPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                        sampleStride);
    }

    file.setFrameBuffer(frameBuffer);

    // Read Sample Counts
    file.readPixelSampleCounts(dataWindow.min.y, dataWindow.max.y);

    // Calculate total samples and allocate
    size_t totalSamples = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            totalSamples += sampleCounts[y][x];
        }
    }

    DeepImageBuffer deepbuf(width, height, totalSamples, sampleCounts);

    // Now populate the pointers in our arrays
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned int count = sampleCounts[y][x];
            if (count > 0) {
                DeepSample* firstSample = &deepbuf.GetMutablePixel(x, y)[0];
                float* rawColor = reinterpret_cast<float*>(&firstSample->color);

                rPtrs[y][x] = &rawColor[RED];
                gPtrs[y][x] = &rawColor[GREEN];
                bPtrs[y][x] = &rawColor[BLUE];
                aPtrs[y][x] = &firstSample->alpha;
                zPtrs[y][x] = &firstSample->z_front;

                if (hasZBack) {
                    zBackPtrs[y][x] = &firstSample->z_back;
                } else {
                    zBackPtrs[y][x] = nullptr;
                }
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

    // Read Pixels (pointers in rPtrs etc. are now valid)
    file.readPixels(dataWindow.min.y, dataWindow.max.y);

    // Handle missing ZBack channel manually
    if (!hasZBack) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                MutableDeepPixelView pixel = deepbuf.GetMutablePixel(x, y);
                for (size_t i = 0; i < pixel.count; ++i) {
                    pixel[i].z_back = pixel[i].z_front;
                }
            }
        }
    }

    return deepbuf;
}

void ImageIO::SaveEXR(DeepImageBuffer& buf, const std::string filename) {
    const int width = buf.GetWidth();
    const int height = buf.GetHeight();

    Imath::Box2i dataWindow(Imath::V2i(0, 0), Imath::V2i(width - 1, height - 1));
    Imf::Header header(width, height, dataWindow);
    int minX = dataWindow.min.x;
    int minY = dataWindow.min.y;

    header.channels().insert("R", Imf_3_2::Channel(Imf_3_2::FLOAT));
    header.channels().insert("G", Imf_3_2::Channel(Imf_3_2::FLOAT));
    header.channels().insert("B", Imf_3_2::Channel(Imf_3_2::FLOAT));
    header.channels().insert("A", Imf_3_2::Channel(Imf_3_2::FLOAT));
    header.channels().insert("Z", Imf_3_2::Channel(Imf_3_2::FLOAT));
    header.channels().insert("ZBack", Imf_3_2::Channel(Imf_3_2::FLOAT));
    header.setType(Imf_3_2::DEEPSCANLINE);
    header.compression() = Imf_3_2::ZIPS_COMPRESSION;

    Imf_3_2::DeepScanLineOutputFile file(filename.c_str(), header);

    auto sampleCounts = Imf::Array2D<unsigned int>(height, width);

    Imf::Array2D<float*> rPtrs(height, width);
    Imf::Array2D<float*> gPtrs(height, width);
    Imf::Array2D<float*> bPtrs(height, width);
    Imf::Array2D<float*> aPtrs(height, width);
    Imf::Array2D<float*> zPtrs(height, width);
    Imf::Array2D<float*> zBackPtrs(height, width);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            MutableDeepPixelView pixel = buf.GetMutablePixel(x, y);
            unsigned int count = static_cast<unsigned int>(pixel.count);

            sampleCounts[y][x] = count;

            if (count > 0) {
                rPtrs[y][x] = &pixel.data->color.data()[RED];
                gPtrs[y][x] = &pixel.data->color.data()[GREEN];
                bPtrs[y][x] = &pixel.data->color.data()[BLUE];
                aPtrs[y][x] = &pixel.data->alpha;
                zPtrs[y][x] = &pixel.data->z_front;
                zBackPtrs[y][x] = &pixel.data->z_back;
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

    Imf_3_2::DeepFrameBuffer frameBuffer;

    size_t countXStride = sizeof(unsigned int);
    size_t countYStride = countXStride * width;

    frameBuffer.insertSampleCountSlice(Imf::Slice(
        Imf_3_2::UINT,
        makeBasePointer(&sampleCounts[0][0], minX, minY, width, countXStride, countYStride),
        countXStride, countYStride));

    size_t sampleStride = sizeof(DeepSample);

    insertDeepSlice(frameBuffer, "R", &rPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "G", &gPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "B", &bPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "A", &aPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "Z", &zPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);
    insertDeepSlice(frameBuffer, "ZBack", &zBackPtrs[0][0], Imf_3_2::FLOAT, minX, minY, width,
                    sampleStride);

    file.setFrameBuffer(frameBuffer);
    file.writePixels(height);

    std::clog << "Saved EXR: " << filename << std::endl;
}

}  // namespace skwr
