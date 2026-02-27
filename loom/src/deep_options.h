#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

struct Options {
    std::vector<std::string> inputFiles;
    std::vector<float> inputZOffsets; // Merge the two later
    std::string outputPrefix;
    bool deepOutput = false;
    bool flatOutput = true;
    bool pngOutput = true;
    bool verbose = false;
    float mergeThreshold = 0.001f;
    bool showHelp = false;
    bool modOffset = false;
    bool enableMerging = true;       // Whether to merge nearby samples
};