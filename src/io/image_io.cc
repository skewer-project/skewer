#include "image_io.h"

#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfDeepScanLineOutputFile.h>
#include <ImfHeader.h>
#include <ImfPartType.h>

#include <cassert>
#include <optional>

#include "ImfMultiPartInputFile.h"
#include "ImfPixelType.h"
#include "film/image_buffer.h"
#include "half.h"

namespace skwr {

#define POINTER_SIZE 4
#define ZBACK 5

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
static void insertDeepSlice(Imf::DeepFrameBuffer& fb, const char* name, void** ptrs,
                            Imf_3_1::PixelType pixelType, int minX, int minY, int width) {
    fb.insert(name,
              Imf::DeepSlice(pixelType, makeBasePointer(ptrs, minX, minY, width), POINTER_SIZE,
                             POINTER_SIZE * width, getPixelTypeSize(pixelType)));
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

// TODO: detect z_back channel in list of file.header().channels();
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

    // defined by the positions of the pixels in the upper left and lower right corners, (x min, y
    // min) and (x max, y max). Imath::Box2i displayWindow = header.displayWindow(); // don't really
    // need it here

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
    auto sampleCount = Imf::Array2D<unsigned int>(height, width);

    // Insert the sample count slice
    //
    // We can't use the simpler setFrameBuffer interface for this part easily without constructing a
    // partial FrameBuffer, but reading sample counts is a specific operation in
    // DeepScanLineInputFile.
    //
    // However, readPixelSampleCounts DOES NOT look at the setFrameBuffer. It just reads the counts.
    // The frame buffer is only needed for readPixels.
    file.readPixelSampleCounts(dataWindow.min.y, dataWindow.max.y);

    // Calculate total samples to allocate contiguous memory
    size_t totalSamples = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            totalSamples += sampleCount[y][x];
        }
    }

    std::clog << "    Total samples: " << totalSamples << std::endl;

    // Allocate contiguous storage for all sample data
    // Using vectors ensures RAII cleanup and better cache locality.
    std::vector<half> rData(totalSamples);
    std::vector<half> gData(totalSamples);
    std::vector<half> bData(totalSamples);
    std::vector<half> aData(totalSamples);
    std::vector<float> zData(totalSamples);
    std::vector<float> zBackData(hasZBack ? totalSamples : 0);

    // Create pointer arrays that OpenEXR expects (one pointer per pixel)
    // These pointers will point into our contiguous vectors.
    Imf::Array2D<half*> rPtrs(height, width);
    Imf::Array2D<half*> gPtrs(height, width);
    Imf::Array2D<half*> bPtrs(height, width);
    Imf::Array2D<half*> aPtrs(height, width);
    Imf::Array2D<float*> zPtrs(height, width);
    std::optional<Imf::Array2D<float*>> zBackPtrs;
    if (hasZBack) {
        zBackPtrs.emplace(height, width);
    }

    // Set up pointers for the deep slices
    size_t offset = 0;  // offset corresponds to the space needed for the number of samples inserted
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned int count = sampleCount[y][x];
            if (count > 0) {
                rPtrs[y][x] = rData.data() + offset;
                gPtrs[y][x] = gData.data() + offset;
                bPtrs[y][x] = bData.data() + offset;
                aPtrs[y][x] = aData.data() + offset;
                zPtrs[y][x] = zData.data() + offset;
                if (hasZBack) {
                    (*zBackPtrs)[y][x] = zBackData.data() + offset;
                }
                offset += count;
            } else {
                rPtrs[y][x] = nullptr;
                gPtrs[y][x] = nullptr;
                bPtrs[y][x] = nullptr;
                aPtrs[y][x] = nullptr;
                zPtrs[y][x] = nullptr;
                if (hasZBack) {
                    (*zBackPtrs)[y][x] = nullptr;
                }
            }
        }
    }

    // 5. Configure FrameBuffer
    Imf::DeepFrameBuffer frameBuffer;

    // Insert sample count slice (still needed for readPixels validation internally by OpenEXR)
    frameBuffer.insertSampleCountSlice(Imf::Slice(
        Imf_3_1::UINT, (char*)(&sampleCount[0][0] - dataWindow.min.x - dataWindow.min.y * width),
        sizeof(unsigned int) * 1,        // xStride
        sizeof(unsigned int) * width));  // yStride

    insertDeepSlice(frameBuffer, "R", (void**)&rPtrs[0][0], Imf_3_1::HALF, minX, minY, width);
    insertDeepSlice(frameBuffer, "G", (void**)&gPtrs[0][0], Imf_3_1::HALF, minX, minY, width);
    insertDeepSlice(frameBuffer, "B", (void**)&bPtrs[0][0], Imf_3_1::HALF, minX, minY, width);
    insertDeepSlice(frameBuffer, "A", (void**)&aPtrs[0][0], Imf_3_1::HALF, minX, minY, width);
    insertDeepSlice(frameBuffer, "Z", (void**)&zPtrs[0][0], Imf_3_1::FLOAT, minX, minY, width);
    if (hasZBack) {
        insertDeepSlice(frameBuffer, "ZBack", (void**)&((*zBackPtrs)[0][0]), Imf_3_1::FLOAT, minX,
                        minY, width);
    }

    file.setFrameBuffer(frameBuffer);

    // Read the pixels, filling each ptr with the corresponding channel info per sample, based on
    // the slices
    file.readPixels(dataWindow.min.y, dataWindow.max.y);

    // Populate DeepImageBuffer
    DeepImageBuffer deepbuf(width, height);

    // NOTE: If we wanted to be super efficient, we could make DeepImageBuffer wrap these vectors
    // directly, but DeepImageBuffer currently uses Array-of-Structures (vector<DeepSample>), so we
    // must copy/convert.
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned int count = sampleCount[y][x];
            if (count == 0) continue;

            DeepPixel& pixel = deepbuf.GetPixel(x, y);  // Get ref to fill

            // Get pointers for this pixel
            // We can reuse the ptrs array, or calculate offset again. Using ptrs array is easier.
            const half* pR = rPtrs[y][x];
            const half* pG = gPtrs[y][x];
            const half* pB = bPtrs[y][x];
            const half* pA = aPtrs[y][x];
            const float* pZ = zPtrs[y][x];
            const float* pZBack = hasZBack ? (*zBackPtrs)[y][x] : nullptr;

            for (unsigned int i = 0; i < count; i++) {
                DeepSample sample;
                sample.alpha = pA[i];
                sample.color = Spectrum(pR[i], pG[i], pB[i]);
                sample.z_front = pZ[i];

                if (hasZBack) {
                    sample.z_back = pZBack[i];
                } else {
                    sample.z_back = sample.z_front;  // Or some default
                }

                pixel.samples.push_back(sample);
            }
        }
    }

    return deepbuf;
}

// OpenEXR File Layout: https://openexr.com/en/latest/OpenEXRFileLayout.html

/*
 * Strategy for `SaveEXR`:
    1. Create Temporary Pointer Arrays: Create std::vector<float*> zPtrs, std::vector<half*> rPtrs,
 etc., that are width * height in size.
    2. Point to your Data: Loop through your DeepImageBuffer. For each pixel, point the zPtrs[i] to
 &pixel.samples[0].Z. NOTE: DeepSample struct must be standard layout. If you have struct { float Z;
 Spectrum c; }, the stride for Z is sizeof(DeepSample).
    3. Configure FrameBuffer:
        - insert("Z", DeepSlice(FLOAT, (char*)zPtrs.data()..., sizeof(float*), sizeof(float*)*width,
 sizeof(DeepSample)))
        - Note the last argument!!! The sampleStride is now sizeof(DeepSample), not sizeof(float).
 This tells OpenEXR to skip over the Color bytes to find the next Z value.
 */
