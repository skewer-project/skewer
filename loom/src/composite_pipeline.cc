#include "composite_pipeline.h"

#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>

#include <stdexcept>
#include <string>

namespace exrio {

std::vector<DeepImage> LoadImagesPhase(const std::vector<std::string>& inputFiles) {
    log("Loading " + std::to_string(inputFiles.size()) + " inputs...");
    
    if (inputFiles.empty()) {
        throw std::runtime_error("No input files provided for loading phase (PartialDeepExrUris is empty)");
    }

    Timer loadTimer;

    // Reserve space for all images upfront to avoid reallocations
    std::vector<DeepImage> images;
    images.reserve(inputFiles.size());

    // Load each image and add it to the vector
    for (size_t i = 0; i < inputFiles.size(); ++i) {
        const std::string& filename = inputFiles[i];

        logVerbose("  [" + std::to_string(i + 1) + "/" + std::to_string(inputFiles.size()) + "] " + filename);

        // Explicit check with a clear error message for the logs
        if (!fileExists(filename)) {
            throw std::runtime_error("Loom cannot find input file: " + filename + 
                                     " (Check if volume mounts or path translation are correct)");
        }

        try {
            if (!isDeepEXR(filename)) {
                throw std::runtime_error("File is not a deep EXR: " + filename);
            }

            // Load the image and add it to the vector while also logging stats
            DeepImage img = loadDeepEXR(filename);

            std::string stats = "    " + std::to_string(img.width()) + "x" + std::to_string(img.height()) + ", " +
                                formatNumber(img.totalSampleCount()) + " total samples (avg " +
                                std::to_string(img.averageSamplesPerPixel()).substr(0, 4) + " samples/pixel)";
            logVerbose(stats);

            // Check for dimension mismatches
            if (!images.empty()) {
                if (img.width() != images[0].width() || img.height() != images[0].height()) {
                    throw std::runtime_error("Image dimensions mismatch: " + filename);
                }
            }

            images.push_back(std::move(img));

        } catch (const DeepReaderException& e) {
            throw std::runtime_error("Failed to load " + filename + ": " + e.what());
        }
    }

    logVerbose("  Load time: " + loadTimer.elapsedString());
    return images;
}

// Flatten the deep image into a standard 2D image.
std::vector<float> FlattenPhase(const DeepImage& mergedImage) {
    // Basically a wrapper to also encapsulate logging
    log("\nFlattening...");
    Timer flattenTimer;

    std::vector<float> flatRgba = flattenImage(mergedImage);

    logVerbose("  Flatten time: " + flattenTimer.elapsedString());
    return flatRgba;
}

// Write the results back to disk using exrio's write functions.
void WriteOutputsPhase(const DeepImage& mergedImage,
                       const std::vector<float>& flatRgba,
                       const std::string& outputPrefix,
                       bool deepOutput,
                       bool flatOutput,
                       bool pngOutput) {
    log("\nWriting outputs...");
    Timer writeTimer;

    // Use exrio's write functions to write the outputs (deep or flat)
    try {
        if (deepOutput) {
            std::string deepPath = outputPrefix + "_merged.exr";
            writeDeepEXR(mergedImage, deepPath);
            log("  Wrote: " + deepPath);
        }

        if (flatOutput) {
            std::string flatPath = outputPrefix + "_flat.exr";
            writeFlatEXR(flatRgba, mergedImage.width(), mergedImage.height(), flatPath);
            log("  Wrote: " + flatPath);
        }

        if (pngOutput) {
            std::string pngPath = outputPrefix;
            // Ensure we have an extension if not provided
            if (pngPath.find('.') == std::string::npos) {
                pngPath += ".png";
            }

            if (hasPNGSupport()) {
                writePNG(flatRgba, mergedImage.width(), mergedImage.height(), pngPath);
                log("  Wrote: " + pngPath);
            } else {
                log("  Skipped PNG (libpng not available)");
            }
        }
    } catch (const DeepWriterException& e) {
        throw std::runtime_error("Failed to write output: " + std::string(e.what()));
    }

    logVerbose("  Write time: " + writeTimer.elapsedString());
}

} // namespace exrio
