#include "deep_writer.h"
#include "utils.h"

#include <OpenEXR/ImfDeepScanLineOutputFile.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfPartType.h>

#ifdef HAS_PNG_SUPPORT
#include <png.h>
#endif

#include <cmath>
#include <algorithm>

namespace deep_compositor {

// ============================================================================
// Flattening Operations
// ============================================================================

std::array<float, 4> flattenPixel(const DeepPixel& pixel) {
    // Front-to-back over operation
    // accum_rgb = accum_rgb + sample_rgb * (1 - accum_alpha)
    // accum_alpha = accum_alpha + sample_alpha * (1 - accum_alpha)
    
    float accumR = 0.0f;
    float accumG = 0.0f;
    float accumB = 0.0f;
    float accumA = 0.0f;
    
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
    
    return {accumR, accumG, accumB, accumA};
}

std::vector<float> flattenImage(const DeepImage& img) {
    int width = img.width();
    int height = img.height();
    
    std::vector<float> result(static_cast<size_t>(width) * height * 4);
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto rgba = flattenPixel(img.pixel(x, y));
            
            size_t idx = (static_cast<size_t>(y) * width + x) * 4;
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

void writeDeepEXR(const DeepImage& img, const std::string& filename) {
    logVerbose("  Writing deep EXR: " + filename);
    
    int width = img.width();
    int height = img.height();
    
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
    size_t totalSamples = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>(y) * width + x;
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
    size_t offset = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>(y) * width + x;
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
        Imf::DeepScanLineOutputFile outFile(filename.c_str(), header);
        
        // Set up frame buffer
        Imf::DeepFrameBuffer frameBuffer;
        
        frameBuffer.insertSampleCountSlice(
            Imf::Slice(
                Imf::UINT,
                reinterpret_cast<char*>(sampleCounts.data()),
                sizeof(unsigned int),
                sizeof(unsigned int) * width
            )
        );
        
        frameBuffer.insert(
            "R",
            Imf::DeepSlice(
                Imf::FLOAT,
                reinterpret_cast<char*>(rPtrs.data()),
                sizeof(float*),
                sizeof(float*) * width,
                sizeof(float)
            )
        );
        
        frameBuffer.insert(
            "G",
            Imf::DeepSlice(
                Imf::FLOAT,
                reinterpret_cast<char*>(gPtrs.data()),
                sizeof(float*),
                sizeof(float*) * width,
                sizeof(float)
            )
        );
        
        frameBuffer.insert(
            "B",
            Imf::DeepSlice(
                Imf::FLOAT,
                reinterpret_cast<char*>(bPtrs.data()),
                sizeof(float*),
                sizeof(float*) * width,
                sizeof(float)
            )
        );
        
        frameBuffer.insert(
            "A",
            Imf::DeepSlice(
                Imf::FLOAT,
                reinterpret_cast<char*>(aPtrs.data()),
                sizeof(float*),
                sizeof(float*) * width,
                sizeof(float)
            )
        );
        
        frameBuffer.insert(
            "Z",
            Imf::DeepSlice(
                Imf::FLOAT,
                reinterpret_cast<char*>(zPtrs.data()),
                sizeof(float*),
                sizeof(float*) * width,
                sizeof(float)
            )
        );

        frameBuffer.insert(
            "ZBack",
            Imf::DeepSlice(
                Imf::FLOAT,
                reinterpret_cast<char*>(zBackPtrs.data()),
                sizeof(float*),
                sizeof(float*) * width,
                sizeof(float)
            )
        );

        outFile.setFrameBuffer(frameBuffer);
        outFile.writePixels(height);
        
    } catch (const std::exception& e) {
        throw DeepWriterException("Failed to write deep EXR: " + std::string(e.what()));
    }
    
    logVerbose("    Wrote " + formatNumber(totalSamples) + " samples");
}

// ============================================================================
// Flat EXR Writing
// ============================================================================

void writeFlatEXR(const DeepImage& img, const std::string& filename) {
    auto rgba = flattenImage(img);
    writeFlatEXR(rgba, img.width(), img.height(), filename);
}

void writeFlatEXR(const std::vector<float>& rgba, 
                  int width, int height, 
                  const std::string& filename) {
    logVerbose("  Writing flat EXR: " + filename);
    
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
            size_t srcIdx = (static_cast<size_t>(y) * width + x) * 4;
            size_t dstIdx = static_cast<size_t>(y) * width + x;
            
            rData[dstIdx] = rgba[srcIdx + 0];
            gData[dstIdx] = rgba[srcIdx + 1];
            bData[dstIdx] = rgba[srcIdx + 2];
            aData[dstIdx] = rgba[srcIdx + 3];
        }
    }
    
    try {
        Imf::OutputFile outFile(filename.c_str(), header);
        
        Imf::FrameBuffer frameBuffer;
        
        frameBuffer.insert("R",
            Imf::Slice(Imf::FLOAT,
                reinterpret_cast<char*>(rData.data()),
                sizeof(float),
                sizeof(float) * width
            )
        );
        
        frameBuffer.insert("G",
            Imf::Slice(Imf::FLOAT,
                reinterpret_cast<char*>(gData.data()),
                sizeof(float),
                sizeof(float) * width
            )
        );
        
        frameBuffer.insert("B",
            Imf::Slice(Imf::FLOAT,
                reinterpret_cast<char*>(bData.data()),
                sizeof(float),
                sizeof(float) * width
            )
        );
        
        frameBuffer.insert("A",
            Imf::Slice(Imf::FLOAT,
                reinterpret_cast<char*>(aData.data()),
                sizeof(float),
                sizeof(float) * width
            )
        );
        
        outFile.setFrameBuffer(frameBuffer);
        outFile.writePixels(height);
        
    } catch (const std::exception& e) {
        throw DeepWriterException("Failed to write flat EXR: " + std::string(e.what()));
    }
}

// ============================================================================
// PNG Writing
// ============================================================================

bool hasPNGSupport() {
#ifdef HAS_PNG_SUPPORT
    return true;
#else
    return false;
#endif
}

void writePNG(const DeepImage& img, const std::string& filename) {
    auto rgba = flattenImage(img);
    writePNG(rgba, img.width(), img.height(), filename);
}

void writePNG(const std::vector<float>& rgba, 
              int width, int height, 
              const std::string& filename) {
#ifndef HAS_PNG_SUPPORT
    (void)rgba;
    (void)width;
    (void)height;
    (void)filename;
    throw DeepWriterException("PNG support not compiled in");
#else
    logVerbose("  Writing PNG: " + filename);
    
    if (width <= 0 || height <= 0) {
        throw DeepWriterException("Invalid image dimensions");
    }
    
    // Open file
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        throw DeepWriterException("Failed to open PNG file for writing: " + filename);
    }
    
    // Create PNG structures
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, 
                                               nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        throw DeepWriterException("Failed to create PNG write struct");
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
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
    png_set_IHDR(
        png, info,
        width, height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    
    png_write_info(png, info);
    
    // Convert float RGBA to 8-bit with simple tone mapping
    std::vector<uint8_t> rowData(static_cast<size_t>(width) * 4);
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t srcIdx = (static_cast<size_t>(y) * width + x) * 4;
            size_t dstIdx = static_cast<size_t>(x) * 4;
            
            // Get premultiplied colors
            float r = rgba[srcIdx + 0];
            float g = rgba[srcIdx + 1];
            float b = rgba[srcIdx + 2];
            float a = rgba[srcIdx + 3];
            
            // Un-premultiply for display (if alpha > 0)
            if (a > 0.0001f) {
                r /= a;
                g /= a;
                b /= a;
            }
            
            // Simple Reinhard tone mapping for HDR values
            auto toneMap = [](float v) -> float {
                return v / (1.0f + v);
            };
            
            // Apply tone mapping only if values exceed 1.0
            if (r > 1.0f || g > 1.0f || b > 1.0f) {
                float maxVal = std::max({r, g, b});
                if (maxVal > 1.0f) {
                    float scale = toneMap(maxVal) / maxVal;
                    r *= scale;
                    g *= scale;
                    b *= scale;
                }
            }
            
            // Clamp and convert to 8-bit
            auto toU8 = [](float v) -> uint8_t {
                return static_cast<uint8_t>(clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            
            rowData[dstIdx + 0] = toU8(r);
            rowData[dstIdx + 1] = toU8(g);
            rowData[dstIdx + 2] = toU8(b);
            rowData[dstIdx + 3] = toU8(a);
        }
        
        png_bytep rowPtr = rowData.data();
        png_write_row(png, rowPtr);
    }
    
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
#endif
}

} // namespace deep_compositor
