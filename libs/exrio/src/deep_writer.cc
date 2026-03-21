#include <ImfCompression.h>
#include <ImfFrameBuffer.h>
#include <ImfPixelType.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineOutputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfOutputFile.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>
#include <pngconf.h>

#include <cstddef>
#include <cstdint>

#include "exrio/deep_image.h"

#ifdef HAS_PNG_SUPPORT
#include <png.h>
#endif

#include <algorithm>
#include <cmath>

namespace exrio {

// ============================================================================
// Flattening Operations
// ============================================================================

static std::array<float, 4> FlattenPixel(const DeepPixel& pixel) {
    // Front-to-back over operation
    // accum_rgb = accum_rgb + sample_rgb * (1 - accum_alpha)
    // accum_alpha = accum_alpha + sample_alpha * (1 - accum_alpha)

    float accum_r = 0.0F;
    float accum_g = 0.0F;
    float accum_b = 0.0F;
    float accum_a = 0.0F;

    for (const auto& sample : pixel.samples()) {
        float oneMinusAccumA = 1.0f - accumA;

        // Since colors are premultiplied, we composite directly
        accumR += sample.red * oneMinusAccumA;
        accumG += sample.green * oneMinusAccumA;
        accumB += sample.blue * oneMinusAccumA;
        accumA += sample.alpha * oneMinusAccumA;

        // Early out if fully opaque
        if (accumA >= 0.9999f) {
            accumA = 1.0f;
            break;
        }
    }

    return {accum_r, accum_g, accum_b, accum_a};
}

static std::vector<float> FlattenImage(const DeepImage& img) {
    int const width = img.width();
    int const height = img.height();

    std::vector<float> result(static_cast<size_t>(width) * height * 4);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto rgba = flattenPixel(img.pixel(x, y));

            size_t const idx = (static_cast<size_t>(y) * width + x) * 4;
            result[idx + 0] = rgba[0];
            result[idx + 1] = rgba[1];
            result[idx + 2] = rgba[2];
            result[idx + 3] = rgba[3];
        }
    }

    return result;
}

// ============================================================================
// Deep EXR Writing
// ============================================================================

static void WriteDeepExr(const DeepImage& img, const std::string& filename) {
    logVerbose("  Writing deep EXR: " + filename);
    ensureDirectoryExists(filename);

    int const width = img.width();
    int const height = img.height();

    if (width <= 0 || height <= 0) {
        throw DeepWriterException("Invalid image dimensions");
    }

    // Set up header
    Imf::Header header(width, height);
    header.setType(Imf::DEEPSCANLINE);

    // Deep images require ZIPS compression (or NO_COMPRESSION)
    header.compression() = Imf::ZIPS_COMPRESSION;

    header.channels().insert("R", Imf::Channel(Imf::FLOAT));
    header.channels().insert("G", Imf::Channel(Imf::FLOAT));
    header.channels().insert("B", Imf::Channel(Imf::FLOAT));
    header.channels().insert("A", Imf::Channel(Imf::FLOAT));
    header.channels().insert("Z", Imf::Channel(Imf::FLOAT));
    header.channels().insert("ZBack", Imf::Channel(Imf::FLOAT));

    // Prepare sample count array
    std::vector<unsigned int> sampleCounts(static_cast<size_t>(width) * height);

    // Count total samples
    size_t const total_samples = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t const idx = (static_cast<size_t>(y) * width) + x;
            sampleCounts[idx] = static_cast<unsigned int>(img.pixel(x, y).sampleCount());
            totalSamples += sampleCounts[idx];
        }
    }

    // Allocate sample data arrays
    std::vector<float> rData(totalSamples);
    std::vector<float> gData(totalSamples);
    std::vector<float> bData(totalSamples);
    std::vector<float> aData(totalSamples);
    std::vector<float> zData(totalSamples);
    std::vector<float> zBackData(totalSamples);

    // Allocate pointer arrays
    std::vector<float*> rPtrs(sampleCounts.size());
    std::vector<float*> gPtrs(sampleCounts.size());
    std::vector<float*> bPtrs(sampleCounts.size());
    std::vector<float*> aPtrs(sampleCounts.size());
    std::vector<float*> zPtrs(sampleCounts.size());
    std::vector<float*> zBackPtrs(sampleCounts.size());

    // Fill data and set up pointers
    size_t const offset = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t const idx = (static_cast<size_t>(y) * width) + x;
            const DeepPixel& pixel = img.pixel(x, y);

            if (sampleCounts[idx] > 0) {
                rPtrs[idx] = rData.data() + offset;
                gPtrs[idx] = gData.data() + offset;
                bPtrs[idx] = bData.data() + offset;
                aPtrs[idx] = aData.data() + offset;
                zPtrs[idx] = zData.data() + offset;
                zBackPtrs[idx] = zBackData.data() + offset;

                for (size_t s = 0; s < pixel.sampleCount(); ++s) {
                    const DeepSample& sample = pixel[s];
                    rData[offset + s] = sample.red;
                    gData[offset + s] = sample.green;
                    bData[offset + s] = sample.blue;
                    aData[offset + s] = sample.alpha;
                    zData[offset + s] = sample.depth;
                    zBackData[offset + s] = sample.depth_back;
                }

                offset += sampleCounts[idx];
            } else {
                rPtrs[idx] = nullptr;
                gPtrs[idx] = nullptr;
                bPtrs[idx] = nullptr;
                aPtrs[idx] = nullptr;
                zPtrs[idx] = nullptr;
                zBackPtrs[idx] = nullptr;
            }
        }
    }

    // Create output file
    try {
        Imf::DeepScanLineOutputFile out_file(filename.c_str(), header);

        // Set up frame buffer
        Imf::DeepFrameBuffer const frame_buffer;

        frame_buffer.insertSampleCountSlice(
            Imf::Slice(Imf::UINT, reinterpret_cast<char*>(sampleCounts.data()),
                       sizeof(unsigned int), sizeof(unsigned int) * width));

        frame_buffer.insert(
            "R", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char*>(rPtrs.data()), sizeof(float*),
                                sizeof(float*) * width, sizeof(float)));

        frame_buffer.insert(
            "G", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char*>(gPtrs.data()), sizeof(float*),
                                sizeof(float*) * width, sizeof(float)));

        frame_buffer.insert(
            "B", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char*>(bPtrs.data()), sizeof(float*),
                                sizeof(float*) * width, sizeof(float)));

        frame_buffer.insert(
            "A", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char*>(aPtrs.data()), sizeof(float*),
                                sizeof(float*) * width, sizeof(float)));

        frame_buffer.insert(
            "Z", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char*>(zPtrs.data()), sizeof(float*),
                                sizeof(float*) * width, sizeof(float)));

        frame_buffer.insert("ZBack",
                            Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char*>(zBackPtrs.data()),
                                           sizeof(float*), sizeof(float*) * width, sizeof(float)));

        out_file.setFrameBuffer(frame_buffer);
        out_file.writePixels(height);

    } catch (const std::exception& e) {
        throw DeepWriterException("Failed to write deep EXR: " + std::string(e.what()));
    }

    logVerbose("    Wrote " + formatNumber(totalSamples) + " samples");
}

// ============================================================================
// Flat EXR Writing
// ============================================================================

static void WriteFlatExr(const DeepImage& img, const std::string& filename) {
    auto rgba = flattenImage(img);
    writeFlatEXR(rgba, img.width(), img.height(), filename);
}

static void WriteFlatExr(const std::vector<float>& rgba, int width, int height,
                         const std::string& filename) {
    logVerbose("  Writing flat EXR: " + filename);
    ensureDirectoryExists(filename);

    if (width <= 0 || height <= 0) {
        throw DeepWriterException("Invalid image dimensions");
    }

    // Set up header
    Imf::Header header(width, height);
    header.channels().insert("R", Imf::Channel(Imf::FLOAT));
    header.channels().insert("G", Imf::Channel(Imf::FLOAT));
    header.channels().insert("B", Imf::Channel(Imf::FLOAT));
    header.channels().insert("A", Imf::Channel(Imf::FLOAT));

    // Separate channels
    std::vector<float> rData(static_cast<size_t>(width) * height);
    std::vector<float> gData(static_cast<size_t>(width) * height);
    std::vector<float> bData(static_cast<size_t>(width) * height);
    std::vector<float> aData(static_cast<size_t>(width) * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t const src_idx = (static_cast<size_t>(y) * width + x) * 4;
            size_t const dst_idx = (static_cast<size_t>(y) * width) + x;

            rData[dstIdx] = rgba[srcIdx + 0];
            gData[dstIdx] = rgba[srcIdx + 1];
            bData[dstIdx] = rgba[srcIdx + 2];
            aData[dstIdx] = rgba[srcIdx + 3];
        }
    }

    try {
        Imf::OutputFile out_file(filename.c_str(), header);

        Imf::FrameBuffer const frame_buffer{};

        frame_buffer.insert("R", Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(rData.data()),
                                            sizeof(float), sizeof(float) * width));

        frame_buffer.insert("G", Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(gData.data()),
                                            sizeof(float), sizeof(float) * width));

        frame_buffer.insert("B", Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(bData.data()),
                                            sizeof(float), sizeof(float) * width));

        frame_buffer.insert("A", Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(aData.data()),
                                            sizeof(float), sizeof(float) * width));

        out_file.setFrameBuffer(frame_buffer);
        out_file.writePixels(height);

    } catch (const std::exception& e) {
        throw DeepWriterException("Failed to write flat EXR: " + std::string(e.what()));
    }
}

// ============================================================================
// PNG Writing
// ============================================================================

auto hasPNGSupport() -> bool {
#ifdef HAS_PNG_SUPPORT
    return true;
#else
    return false;
#endif
}

static void WritePng(const DeepImage& img, const std::string& filename) {
    auto rgba = flattenImage(img);
    writePNG(rgba, img.width(), img.height(), filename);
}

static void WritePng(const std::vector<float>& rgba, int width, int height,
                     const std::string& filename) {
#ifndef HAS_PNG_SUPPORT
    (void)rgba;
    (void)width;
    (void)height;
    (void)filename;
    throw DeepWriterException("PNG support not compiled in");
#else
    logVerbose("  Writing PNG: " + filename);
    ensureDirectoryExists(filename);

    if (width <= 0 || height <= 0) {
        throw DeepWriterException("Invalid image dimensions");
    }

    // Open file
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        throw DeepWriterException("Failed to open PNG file for writing: " + filename);
    }

    // Create PNG structures
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        fclose(fp);
        throw DeepWriterException("Failed to create PNG write struct");
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        throw DeepWriterException("Failed to create PNG info struct");
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        throw DeepWriterException("PNG write error");
    }

    png_init_io(png, fp);

    // Set image properties (RGBA, 8-bit)
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);

    // Convert float RGBA to 8-bit with simple tone mapping
    std::vector<uint8_t> rowData(static_cast<size_t>(width) * 4);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t const src_idx = (static_cast<size_t>(y) * width + x) * 4;
            size_t const dst_idx = static_cast<size_t>(x) * 4;

            // Get premultiplied colors
            float r = rgba[src_idx + 0];
            float g = rgba[src_idx + 1];
            float b = rgba[src_idx + 2];
            float a = rgba[src_idx + 3];

            // Un-premultiply for display (if alpha > 0)
            if (a > 0.0001F) {
                r /= a;
                g /= a;
                b /= a;
            }

            // Per-channel Reinhard tone mapping (handles HDR gracefully)
            r = std::max(0.0f, r);
            g = std::max(0.0f, g);
            b = std::max(0.0f, b);
            r = r / (1.0F + r);
            g = g / (1.0F + g);
            b = b / (1.0F + b);

            // sRGB gamma correction
            r = std::pow(r, 1.0f / 2.2f);
            g = std::pow(g, 1.0f / 2.2f);
            b = std::pow(b, 1.0f / 2.2f);

            // Clamp and convert to 8-bit
            auto to_u8 = [](float v) -> uint8_t {
                return static_cast<uint8_t>((clamp(v, 0.0F, 1.0F) * 255.0F) + 0.5F);
            };

            rowData[dstIdx + 0] = toU8(r);
            rowData[dstIdx + 1] = toU8(g);
            rowData[dstIdx + 2] = toU8(b);
            rowData[dstIdx + 3] = toU8(a);
        }

        png_bytep row_ptr = rowData.data() = nullptr;
        png_write_row(png, row_ptr);
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
#endif
}

}  // namespace exrio
