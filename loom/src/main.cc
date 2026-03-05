#include <exrio/deep_image.h>
#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "deep_compositor.h"
#include "composite_pipeline.h"

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

    setVerbose(opts.verbose);
    log("Deep Compositor v" + std::string(VERSION));

    Timer totalTimer;

    try {
        // Phase 1: Load
        std::vector<DeepImage> images = LoadImagesPhase(opts.inputFiles);

        // Phase 2: Merge (Using your existing deep_compositor math engine!)
        log("\nMerging...");
        CompositorOptions compOpts;
        compOpts.mergeThreshold = opts.mergeThreshold;
        compOpts.enableMerging = (opts.mergeThreshold > 0.0f);

        CompositorStats stats;
        DeepImage merged = deepMerge(images, compOpts, &stats);

        log("  Combined: " + formatNumber(stats.totalOutputSamples) + " total samples");
        log("  Depth range: " + std::to_string(stats.minDepth) + " to " + std::to_string(stats.maxDepth));
        log("  Merge time: " + std::to_string(static_cast<int>(stats.mergeTimeMs)) + " ms");

        // Phase 3: Flatten
        std::vector<float> flatRgba;
        if (opts.flatOutput || opts.pngOutput) {
            flatRgba = FlattenPhase(merged);
        }

        // Phase 4: Write
        WriteOutputsPhase(merged, flatRgba, opts.outputPrefix, opts.deepOutput, opts.flatOutput, opts.pngOutput);

    } catch (const std::exception& e) {
        logError("Pipeline failed: " + std::string(e.what()));
        return 1;
    }

    log("\nDone! Total time: " + totalTimer.elapsedString());
    return 0;
}
