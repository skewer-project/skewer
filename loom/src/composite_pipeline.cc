#include "composite_pipeline.h"

#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>

#include <cstddef>
#include <stdexcept>
#include <string>

#include "deep_options.h"
#include "utils.h"

namespace exrio {

// Write the results back to disk using exrio's write functions.
static void WriteFlatOutputs(const std::vector<float>& flat_rgba, const std::string& output_uri,
                      bool flat_output, bool png_output, int width, int height) {
    log("\nWriting outputs...");
    Timer const write_timer;

    // Use exrio's write functions to write the outputs (deep or flat)
    try {
        if (flat_output) {
            writeFlatEXR(flatRgba, width, height, outputUri);
            log("  Wrote: " + outputUri);
        }

        if (png_output) {
            std::string png_path = outputUri;
            // Ensure we have an extension if not provided
            if (pngPath.find('.') == std::string::npos) {
                png_path += ".png";
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

    logVerbose("  Write time: " + write_timer.elapsedString());
}

static auto SaveImageInfo(const Options& opts,
                  std::vector<std::unique_ptr<deep_compositor::DeepInfo> /*unused*/>& imagesInfo) -> int {
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
