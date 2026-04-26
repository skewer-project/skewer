#include <exrio/deep_image.h>
#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>
#include <exrio/utils.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "composite_pipeline.h"
#include "deep_compositor.h"
#include "deep_options.h"

namespace deep_compositor {

const char* VERSION = "1.0";

bool isFloat(const std::string& s) {
    std::istringstream iss(s);
    float f;
    // Try to read a float, check if successful and no non-whitespace characters remain
    if (!(iss >> f)) {
        return false;  // Conversion failed
    }
    // Check if there are any non-whitespace characters left in the stream
    // Use std::ws to consume any trailing whitespace
    return (iss >> std::ws).eof();
}

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
              << "  --verbose, -v        Detailed Logging\n"
              << "  --merge-threshold N  Depth epsilon for merging samples (default: 0.001)\n"
              << "  --help, -h           Show this help message\n\n"
              << "Example:\n"
              << "  " << programName << " --deep-output --verbose \\\n"
              << "      test_data/ground_plane.exr \\\n"
              << "      output/result\n\n"
              << "      test_data/sphere_front.exr \\\n"
              << "      test_data/sphere_back.exr \\\n"
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
            opts.show_help = true;
            return true;
        } else if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--deep-output") {
            opts.deep_output = true;
        } else if (arg == "--flat-output") {
            opts.flat_output = true;
        } else if (arg == "--no-flat-output") {
            opts.flat_output = false;
        } else if (arg == "--png-output") {
            opts.png_output = true;
        } else if (arg == "--no-png-output") {
            opts.png_output = false;
        } else if (arg == "--mod-offset") {
            opts.mod_offset = true;
        } else if (arg == "--merge-threshold") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --merge-threshold requires a value\n";
                return false;
            }
            try {
                opts.merge_threshold = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid merge threshold value\n";
                return false;
            }
        } else if (arg[0] == '-' && !isFloat(arg)) {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return false;
        } else {
            // Positional argument (input file or output prefix)

            if (opts.mod_offset && isFloat(arg)) {
                if (opts.input_files.size() != opts.input_z_offsets.size() + 1) {
                    std::cerr << "Error: Mismatched position of Z offset value\n";
                    return false;
                }
                try {
                    float offset = std::stof(arg);
                    opts.input_z_offsets.push_back(offset);
                    continue;  // Don't treat this as an input file
                } catch (...) {
                    std::cerr << "Error: Invalid Z offset value: " << arg << "\n";
                    return false;
                }
            } else {
                if (opts.mod_offset &&
                    (opts.input_files.size() == opts.input_z_offsets.size() + 1)) {
                    opts.input_z_offsets.push_back(0);  // Default offset for this file
                }
                opts.input_files.push_back(
                    arg);  // Could be input file or output prefix, we'll determine later
            }
        }
    }
    if (opts.mod_offset && ((opts.input_files.size() - 1) != opts.input_z_offsets.size())) {
        opts.input_z_offsets.push_back(0);  // Default offset for last file if not provided
    }
    // std::cerr << "There are " << opts.inputFiles.size() << " input files and " <<
    // opts.input_z_offsets.size() << " Z offsets\n";

    for (size_t i = 0; i < opts.input_z_offsets.size(); ++i) {
        std::cerr << "Z Offset " << i << ": " << opts.input_z_offsets[i] << "\n";
    }

    // Need at least one input and one output prefix
    if (opts.input_files.size() < 2) {
        std::cerr << "Error: Need at least one input file and an output prefix\n";
        return false;
    }

    // Last positional arg is output prefix
    opts.output_prefix = opts.input_files.back();
    opts.input_files.pop_back();

    return true;
}
}  // namespace deep_compositor
   // anonymous namespace

int main(int argc, char* argv[]) {
    using namespace deep_compositor;

    Options opts;

    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

    if (opts.show_help) {
        printUsage(argv[0]);
        return 0;
    }

    // Set verbose mode
    exrio::setVerbose(opts.verbose);

    exrio::log("Loom v" + std::string(VERSION));

    exrio::Timer totalTimer;

    exrio::log("Loading inputs...");
    exrio::Timer loadTimer;

    std::vector<std::unique_ptr<exrio::DeepStreamReader>> imagesInfo;

    for (size_t i = 0; i < opts.input_files.size(); ++i) {
        const std::string& filename = opts.input_files[i];

        exrio::logVerbose("  [" + std::to_string(i + 1) + "/" + std::to_string(opts.input_files.size()) +
                            "] " + filename);
        printf("Psreloading [%zu/%zu]: %s\n", i + 1, opts.input_files.size(), filename.c_str());
        try {
            // Check if it's a deep EXR
            if (!exrio::isDeepEXR(filename)) {
                exrio::logError("File is not a deep EXR: " + filename);
                return 1;
            }

            auto img = std::make_unique<exrio::DeepStreamReader>(filename);
            // Log statistics
            std::string stats =
                "    " + std::to_string(img->getWidth()) + "x" + std::to_string(img->getHeight());
            exrio::logVerbose(stats);
            if (!imagesInfo.empty()) {
                if (img->getWidth() != imagesInfo[0]->getWidth() ||
                    img->getHeight() != imagesInfo[0]->getHeight()) {
                    exrio::logError("Image dimensions mismatch: " + filename);
                    exrio::logError("  Expected: " + std::to_string(imagesInfo[0]->getWidth()) + "x" +
                                     std::to_string(imagesInfo[0]->getHeight()));
                    exrio::logError("  Got: " + std::to_string(img->getWidth()) + "x" +
                                     std::to_string(img->getHeight()));
                    return 1;
                }
            }

            imagesInfo.push_back(std::move(img));

        } catch (const exrio::DeepReaderException& e) {
            exrio::logError("Failed to load " + filename + ": " + e.what());
            return 1;
        } catch (const std::exception& e) {
            exrio::logError("Unexpected error loading " + filename + ": " + e.what());
            return 1;
        }
        if (!exrio::isDeepEXR(filename)) {
            exrio::logError("File is not a deep EXR: " + filename);
            return 1;
        }
    }
    int height = imagesInfo[0]->getHeight();
    int width = imagesInfo[0]->getWidth();

    exrio::log("Starting processing...");
    std::vector<float> finalImage = ProcessAllEXR(opts, height, width, imagesInfo);

    exrio::log("\nWriting To PNG...");
    exrio::Timer writeTimer;

    try {
        // Write flat EXR if requested
        if (opts.flat_output) {
            std::string flatPath = opts.output_prefix + "_flat.exr";
            exrio::writeFlatEXR(finalImage, width, height, flatPath);
            exrio::log("  Wrote: " + flatPath);
        }

        // Write PNG if requested
        if (opts.png_output) {
            std::string pngPath = opts.output_prefix + ".png";

            if (exrio::hasPNGSupport()) {
                exrio::writePNG(finalImage, width, height, pngPath);
                exrio::log("  Wrote: " + pngPath);
            } else {
                exrio::log("  Skipped PNG (libpng not available)");
            }
        }

    } catch (const exrio::DeepWriterException& e) {
        exrio::logError("Failed to write output: " + std::string(e.what()));
        return 1;
    }

    // exrio::logVerbose("  Write time: " + writeTimer.elapsedString());

    // ========================================================================
    // Summary
    // ========================================================================

    exrio::log("\nDone! Total time: " + totalTimer.elapsedString());

    return 0;
}
