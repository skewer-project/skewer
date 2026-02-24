#include "deep_reader.h"
#include "utils.h"

#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfMultiPartInputFile.h>

#include <vector>
#include <memory>

namespace deep_compositor {

bool isDeepEXR(const std::string& filename) {
    try {
        Imf::MultiPartInputFile file(filename.c_str());
        if (file.parts() < 1) {
            return false;
        }
        
        const Imf::Header& header = file.header(0);
        return header.hasType() && Imf::isDeepData(header.type());
    } catch (...) {
        return false;
    }
}

bool getDeepEXRInfo(const std::string& filename, 
                    int& width, int& height, bool& isDeep) {
    try {
        Imf::MultiPartInputFile file(filename.c_str());
        if (file.parts() < 1) {
            return false;
        }
        
        const Imf::Header& header = file.header(0);
        isDeep = header.hasType() && Imf::isDeepData(header.type());
        
        Imath::Box2i dataWindow = header.dataWindow();
        width = dataWindow.max.x - dataWindow.min.x + 1;
        height = dataWindow.max.y - dataWindow.min.y + 1;
        
        return true;
    } catch (...) {
        return false;
    }
}

DeepImage loadDeepEXR(const std::string& filename) {
    logVerbose("  Opening: " + filename);
    
    // Check if file exists
    if (!fileExists(filename)) {
        throw DeepReaderException("File not found: " + filename);
    }
    
    // Open the deep EXR file
    std::unique_ptr<Imf::DeepScanLineInputFile> file;
    try {
        file = std::make_unique<Imf::DeepScanLineInputFile>(filename.c_str());
    } catch (const std::exception& e) {
        throw DeepReaderException("Failed to open EXR file: " + std::string(e.what()));
    }
    
    const Imf::Header& header = file->header();
    
    // Verify it's a deep image
    if (!header.hasType() || !Imf::isDeepData(header.type())) {
        throw DeepReaderException("File is not a deep EXR: " + filename);
    }
    
    // Get dimensions
    Imath::Box2i dataWindow = header.dataWindow();
    int width = dataWindow.max.x - dataWindow.min.x + 1;
    int height = dataWindow.max.y - dataWindow.min.y + 1;
    int minX = dataWindow.min.x;
    int minY = dataWindow.min.y;
    
    logVerbose("    Resolution: " + std::to_string(width) + "x" + std::to_string(height));
    
    // Check for required channels
    const Imf::ChannelList& channels = header.channels();
    bool hasR = channels.findChannel("R") != nullptr;
    bool hasG = channels.findChannel("G") != nullptr;
    bool hasB = channels.findChannel("B") != nullptr;
    bool hasA = channels.findChannel("A") != nullptr;
    bool hasZ = channels.findChannel("Z") != nullptr;
    bool hasZBack = channels.findChannel("ZBack") != nullptr;

    if (!hasR || !hasG || !hasB || !hasA || !hasZ) {
        std::string missing;
        if (!hasR) missing += "R ";
        if (!hasG) missing += "G ";
        if (!hasB) missing += "B ";
        if (!hasA) missing += "A ";
        if (!hasZ) missing += "Z ";
        throw DeepReaderException("Missing required channels: " + missing);
    }
    
    // Create the result image
    DeepImage result(width, height);
    
    // Allocate sample count array
    std::vector<unsigned int> sampleCounts(static_cast<size_t>(width) * height);
    
    // Prepare pointer arrays before setFrameBuffer(). OpenEXR stores a copy of
    // the frame buffer descriptor, so these arrays must remain stable in memory.
    std::vector<float*> rPtrs(sampleCounts.size(), nullptr);
    std::vector<float*> gPtrs(sampleCounts.size(), nullptr);
    std::vector<float*> bPtrs(sampleCounts.size(), nullptr);
    std::vector<float*> aPtrs(sampleCounts.size(), nullptr);
    std::vector<float*> zPtrs(sampleCounts.size(), nullptr);
    std::vector<float*> zBackPtrs(sampleCounts.size(), nullptr);

    // Use one persistent frame buffer lifecycle: set once, then read sample
    // counts and deep samples. This avoids version-specific state resets.
    Imf::DeepFrameBuffer frameBuffer;

    frameBuffer.insertSampleCountSlice(
        Imf::Slice(
            Imf::UINT,
            reinterpret_cast<char*>(sampleCounts.data() - minX - static_cast<long>(minY) * width),
            sizeof(unsigned int),      // xStride
            sizeof(unsigned int) * width  // yStride
        )
    );

    frameBuffer.insert(
        "R",
        Imf::DeepSlice(
            Imf::FLOAT,
            reinterpret_cast<char*>(rPtrs.data() - minX - static_cast<long>(minY) * width),
            sizeof(float*),           // xStride for pointer array
            sizeof(float*) * width,   // yStride for pointer array
            sizeof(float)             // sample stride
        )
    );

    frameBuffer.insert(
        "G",
        Imf::DeepSlice(
            Imf::FLOAT,
            reinterpret_cast<char*>(gPtrs.data() - minX - static_cast<long>(minY) * width),
            sizeof(float*),
            sizeof(float*) * width,
            sizeof(float)
        )
    );

    frameBuffer.insert(
        "B",
        Imf::DeepSlice(
            Imf::FLOAT,
            reinterpret_cast<char*>(bPtrs.data() - minX - static_cast<long>(minY) * width),
            sizeof(float*),
            sizeof(float*) * width,
            sizeof(float)
        )
    );

    frameBuffer.insert(
        "A",
        Imf::DeepSlice(
            Imf::FLOAT,
            reinterpret_cast<char*>(aPtrs.data() - minX - static_cast<long>(minY) * width),
            sizeof(float*),
            sizeof(float*) * width,
            sizeof(float)
        )
    );

    frameBuffer.insert(
        "Z",
        Imf::DeepSlice(
            Imf::FLOAT,
            reinterpret_cast<char*>(zPtrs.data() - minX - static_cast<long>(minY) * width),
            sizeof(float*),
            sizeof(float*) * width,
            sizeof(float)
        )
    );

    if (hasZBack) {
        frameBuffer.insert(
            "ZBack",
            Imf::DeepSlice(
                Imf::FLOAT,
                reinterpret_cast<char*>(zBackPtrs.data() - minX - static_cast<long>(minY) * width),
                sizeof(float*),
                sizeof(float*) * width,
                sizeof(float)
            )
        );
    }
    
    file->setFrameBuffer(frameBuffer);
    
    // Read sample counts
    file->readPixelSampleCounts(minY, minY + height - 1);
    
    // Calculate total samples and allocate data arrays
    size_t totalSamples = 0;
    for (const auto& count : sampleCounts) {
        totalSamples += count;
    }
    
    logVerbose("    Total samples: " + formatNumber(totalSamples));
    
    // Allocate contiguous storage for all samples
    std::vector<float> rData(totalSamples);
    std::vector<float> gData(totalSamples);
    std::vector<float> bData(totalSamples);
    std::vector<float> aData(totalSamples);
    std::vector<float> zData(totalSamples);
    std::vector<float> zBackData(hasZBack ? totalSamples : 0);
    
    // Set up pointers into the contiguous arrays
    size_t offset = 0;
    for (size_t i = 0; i < sampleCounts.size(); ++i) {
        if (sampleCounts[i] > 0) {
            rPtrs[i] = rData.data() + offset;
            gPtrs[i] = gData.data() + offset;
            bPtrs[i] = bData.data() + offset;
            aPtrs[i] = aData.data() + offset;
            zPtrs[i] = zData.data() + offset;
            if (hasZBack) zBackPtrs[i] = zBackData.data() + offset;
            offset += sampleCounts[i];
        } else {
            rPtrs[i] = nullptr;
            gPtrs[i] = nullptr;
            bPtrs[i] = nullptr;
            aPtrs[i] = nullptr;
            zPtrs[i] = nullptr;
            if (hasZBack) zBackPtrs[i] = nullptr;
        }
    }
    
    // Read the deep pixel data
    file->readPixels(minY, minY + height - 1);
    
    // Convert to our DeepImage format
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t pixelIndex = static_cast<size_t>(y) * width + x;
            unsigned int numSamples = sampleCounts[pixelIndex];
            
            if (numSamples > 0) {
                DeepPixel& pixel = result.pixel(x, y);
                
                for (unsigned int s = 0; s < numSamples; ++s) {
                    DeepSample sample;
                    sample.depth = zPtrs[pixelIndex][s];
                    sample.depth_back = hasZBack ? zBackPtrs[pixelIndex][s] : sample.depth;
                    sample.red = rPtrs[pixelIndex][s];
                    sample.green = gPtrs[pixelIndex][s];
                    sample.blue = bPtrs[pixelIndex][s];
                    sample.alpha = aPtrs[pixelIndex][s];

                    pixel.addSample(sample);
                }
            }
        }
    }
    
    // Verify depth ordering
    if (!result.isValid()) {
        logVerbose("    Warning: Re-sorting samples (input not depth-ordered)");
        result.sortAllPixels();
    }
    
    return result;
}

} // namespace deep_compositor
