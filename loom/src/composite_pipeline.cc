#include "composite_pipeline.h"

#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>

#include <stdexcept>

namespace exrio {

std::vector<DeepImage> LoadImagesPhase(const std::vector<std::string>& inputFiles) {
    log("Loading inputs...");
    Timer loadTimer;

    std::vector<DeepImage> images;
    images.reserve(inputFiles.size());

    for (size_t i = 0; i < inputFiles.size(); ++i) {
        const std::string& filename = inputFiles[i];

        logVerbose("  [" + std::to_string(i + 1) + "/" + std::to_string(inputFiles.size()) + "] " + filename);

        try {
            if (!isDeepEXR(filename)) {
                throw std::runtime_error("File is not a deep EXR: " + filename);
            }

            DeepImage img = loadDeepEXR(filename);

            std::string stats = "    " + std::to_string(img.width()) + "x" + std::to_string(img.height()) + ", " +
                                formatNumber(img.totalSampleCount()) + " total samples (avg " +
                                std::to_string(img.averageSamplesPerPixel()).substr(0, 4) + " samples/pixel)";
            logVerbose(stats);

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

std::vector<float> FlattenPhase(const DeepImage& mergedImage) {
    log("\nFlattening...");
    Timer flattenTimer;

    std::vector<float> flatRgba = flattenImage(mergedImage);

    logVerbose("  Flatten time: " + flattenTimer.elapsedString());
    return flatRgba;
}

void WriteOutputsPhase(const DeepImage& mergedImage,
                       const std::vector<float>& flatRgba,
                       const std::string& outputPrefix,
                       bool deepOutput,
                       bool flatOutput,
                       bool pngOutput) {
    log("\nWriting outputs...");
    Timer writeTimer;

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
            std::string pngPath = outputPrefix + ".png";
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
