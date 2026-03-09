#pragma once

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

struct Options {
    std::vector<std::string> input_files;
    std::vector<float> input_z_offsets;
    std::string output_prefix;
    bool deep_output = false;
    bool flat_output = true;
    bool png_output = true;
    bool verbose = false;
    float merge_threshold = 0.001f;
    bool show_help = false;
    bool mod_offset = false;
    bool enable_merging = true;
};