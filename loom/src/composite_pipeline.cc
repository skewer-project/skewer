#include "composite_pipeline.h"

#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>

#include <stdexcept>
#include <string>

#include "deep_info.h"
#include "utils.h"

namespace exrio {

// Write the results back to disk using exrio's write functions.
void WriteFlatOutputs(const std::vector<float>& flatRgba, const std::string& outputUri,
                      bool flatOutput, bool pngOutput, int width, int height) {
    log("\nWriting outputs...");
    Timer writeTimer;

    // Use exrio's write functions to write the outputs (deep or flat)
    try {
        if (flatOutput) {
            writeFlatEXR(flatRgba, width, height, outputUri);
            log("  Wrote: " + outputUri);
        }

        if (pngOutput) {
            std::string pngPath = outputUri;
            // Ensure we have an extension if not provided
            if (pngPath.find('.') == std::string::npos) {
                pngPath += ".png";
            }

            if (hasPNGSupport()) {
                writePNG(flatRgba, width, height, pngPath);
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

int SaveImageInfo(const Options& opts,
                  std::vector<std::unique_ptr<deep_compositor::DeepInfo>>& imagesInfo) {
    for (size_t i = 0; i < opts.input_files.size(); ++i) {
        const std::string& filename = opts.input_files[i];

        deep_compositor::LogVerbose("  [" + std::to_string(i + 1) + "/" +
                                    std::to_string(opts.input_files.size()) + "] " + filename);
        printf("Preloading [%zu/%zu]: %s\n", i + 1, opts.input_files.size(), filename.c_str());
        try {
            // Check if it's a deep EXR
            if (!exrio::isDeepEXR(filename)) {
                deep_compositor::LogError("File is not a deep EXR: " + filename);
                return 1;
            }

            auto img = std::make_unique<deep_compositor::DeepInfo>(filename);
            // Log statistics
            std::string stats =
                "    " + std::to_string(img->width()) + "x" + std::to_string(img->height());
            deep_compositor::LogVerbose(stats);
            if (!imagesInfo.empty()) {
                if (img->width() != imagesInfo[0]->width() ||
                    img->height() != imagesInfo[0]->height()) {
                    deep_compositor::LogError("Image dimensions mismatch: " + filename);
                    deep_compositor::LogError(
                        "  Expected: " + std::to_string(imagesInfo[0]->width()) + "x" +
                        std::to_string(imagesInfo[0]->height()));
                    deep_compositor::LogError("  Got: " + std::to_string(img->width()) + "x" +
                                              std::to_string(img->height()));
                    return 1;
                }
            }

            imagesInfo.push_back(std::move(img));

        } catch (const exrio::DeepReaderException& e) {
            deep_compositor::LogError("Failed to load " + filename + ": " + e.what());
            return 1;
        } catch (const std::exception& e) {
            deep_compositor::LogError("Unexpected error loading " + filename + ": " + e.what());
            return 1;
        }
        if (!exrio::isDeepEXR(filename)) {
            deep_compositor::LogError("File is not a deep EXR: " + filename);
            return 1;
        }
    }

    return 0;
}

}  // namespace exrio
