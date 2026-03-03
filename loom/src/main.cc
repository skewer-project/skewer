#include <exrio/deep_image.h>
#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "deep_compositor.h"

namespace {

const char* VERSION = "1.0";

struct Options {
    std::vector<std::string> inputFiles;
    std::string outputPrefix;
    bool deepOutput = false;
    bool flatOutput = true;
    bool pngOutput = true;
    bool verbose = false;
    float mergeThreshold = 0.001f;
    bool showHelp = false;
};

void printUsage(const char* programName) {
    std::cout << "Deep Image Compositor v" << VERSION << "\n\n"
              << "Usage: " << programName
              << " [options] <input1.exr> [input2.exr ...] <output_prefix>\n\n"
              << "Options:\n"
              << "  --deep-output        Write merged deep EXR (default: off)\n"
              << "  --flat-output        Write flattened EXR (default: on)\n"
              << "  --no-flat-output     Don't write flattened EXR\n"
              << "  --png-output         Write PNG preview (default: on)\n"
              << "  --no-png-output      Don't write PNG preview\n"
              << "  --verbose, -v        Detailed logging\n"
              << "  --merge-threshold N  Depth epsilon for merging samples (default: 0.001)\n"
              << "  --help, -h           Show this help message\n\n"
              << "Example:\n"
              << "  " << programName << " --deep-output --verbose \\\n"
              << "      demo/inputs/nebula_red.exr \\\n"
              << "      demo/inputs/nebula_green.exr \\\n"
              << "      demo/inputs/backdrop.exr \\\n"
              << "      output/result\n\n"
              << "Outputs:\n"
              << "  <output_prefix>_merged.exr  (deep EXR, if --deep-output)\n"
              << "  <output_prefix>_flat.exr    (standard EXR)\n"
              << "  <output_prefix>.png         (preview image)\n";
}

bool parseArgs(int argc, char* argv[], Options& opts) {
    if (argc < 2) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            opts.showHelp = true;
            return true;
        } else if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--deep-output") {
            opts.deepOutput = true;
        } else if (arg == "--flat-output") {
            opts.flatOutput = true;
        } else if (arg == "--no-flat-output") {
            opts.flatOutput = false;
        } else if (arg == "--png-output") {
            opts.pngOutput = true;
        } else if (arg == "--no-png-output") {
            opts.pngOutput = false;
        } else if (arg == "--merge-threshold") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --merge-threshold requires a value\n";
                return false;
            }
            try {
                opts.mergeThreshold = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid merge threshold value\n";
                return false;
            }
        } else if (arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return false;
        } else {
            // Positional argument (input file or output prefix)
            opts.inputFiles.push_back(arg);
        }
    }

    // Need at least one input and one output prefix
    if (opts.inputFiles.size() < 2) {
        std::cerr << "Error: Need at least one input file and an output prefix\n";
        return false;
    }

    // Last positional arg is output prefix
    opts.outputPrefix = opts.inputFiles.back();
    opts.inputFiles.pop_back();

    return true;
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    using namespace exrio;

    Options opts;

    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

    if (opts.showHelp) {
        printUsage(argv[0]);
        return 0;
    }

    // Set verbose mode
    setVerbose(opts.verbose);

    log("Deep Compositor v" + std::string(VERSION));

    Timer totalTimer;

    // ========================================================================
    // Load Phase
    // ========================================================================
    log("Loading inputs...");
    Timer loadTimer;

    std::vector<DeepImage> images;
    images.reserve(opts.inputFiles.size());

    for (size_t i = 0; i < opts.inputFiles.size(); ++i) {
        const std::string& filename = opts.inputFiles[i];

        logVerbose("  [" + std::to_string(i + 1) + "/" + std::to_string(opts.inputFiles.size()) +
                   "] " + filename);

        try {
            // Check if it's a deep EXR
            if (!isDeepEXR(filename)) {
                logError("File is not a deep EXR: " + filename);
                return 1;
            }

            DeepImage img = loadDeepEXR(filename);

            // Log statistics
            std::string stats =
                "    " + std::to_string(img.width()) + "x" + std::to_string(img.height()) + ", " +
                formatNumber(img.totalSampleCount()) + " total samples (avg " +
                std::to_string(img.averageSamplesPerPixel()).substr(0, 4) + " samples/pixel)";
            logVerbose(stats);

            // Validate dimensions match
            if (!images.empty()) {
                if (img.width() != images[0].width() || img.height() != images[0].height()) {
                    logError("Image dimensions mismatch: " + filename);
                    logError("  Expected: " + std::to_string(images[0].width()) + "x" +
                             std::to_string(images[0].height()));
                    logError("  Got: " + std::to_string(img.width()) + "x" +
                             std::to_string(img.height()));
                    return 1;
                }
            }

            images.push_back(std::move(img));

        } catch (const DeepReaderException& e) {
            logError("Failed to load " + filename + ": " + e.what());
            return 1;
        }
    }

    logVerbose("  Load time: " + loadTimer.elapsedString());

    // ========================================================================
    // Merge Phase
    // ========================================================================
    log("\nMerging...");

    CompositorOptions compOpts;
    compOpts.mergeThreshold = opts.mergeThreshold;
    compOpts.enableMerging = (opts.mergeThreshold > 0.0f);

    CompositorStats stats;

    DeepImage merged = deepMerge(images, compOpts, &stats);

    log("  Combined: " + formatNumber(stats.totalOutputSamples) + " total samples");
    log("  Depth range: " + std::to_string(stats.minDepth) + " to " +
        std::to_string(stats.maxDepth));
    log("  Merge time: " + std::to_string(static_cast<int>(stats.mergeTimeMs)) + " ms");

    // ========================================================================
    // Flatten Phase
    // ========================================================================
    std::vector<float> flatRgba;

    if (opts.flatOutput || opts.pngOutput) {
        log("\nFlattening...");
        Timer flattenTimer;

        flatRgba = flattenImage(merged);

        logVerbose("  Flatten time: " + flattenTimer.elapsedString());
    }

    // ========================================================================
    // Write Phase
    // ========================================================================
    log("\nWriting outputs...");
    Timer writeTimer;

    try {
        // Write deep output if requested
        if (opts.deepOutput) {
            std::string deepPath = opts.outputPrefix + "_merged.exr";
            writeDeepEXR(merged, deepPath);
            log("  Wrote: " + deepPath);
        }

        // Write flat EXR if requested
        if (opts.flatOutput) {
            std::string flatPath = opts.outputPrefix + "_flat.exr";
            writeFlatEXR(flatRgba, merged.width(), merged.height(), flatPath);
            log("  Wrote: " + flatPath);
        }

        // Write PNG if requested
        if (opts.pngOutput) {
            std::string pngPath = opts.outputPrefix + ".png";

            if (hasPNGSupport()) {
                writePNG(flatRgba, merged.width(), merged.height(), pngPath);
                log("  Wrote: " + pngPath);
            } else {
                log("  Skipped PNG (libpng not available)");
            }
        }

    } catch (const DeepWriterException& e) {
        logError("Failed to write output: " + std::string(e.what()));
        return 1;
    }

    logVerbose("  Write time: " + writeTimer.elapsedString());

    // ========================================================================
    // Summary
    // ========================================================================
    log("\nDone! Total time: " + totalTimer.elapsedString());

    return 0;
}
