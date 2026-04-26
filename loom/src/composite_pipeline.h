#pragma once

#include <exrio/deep_image.h>
#include <exrio/deep_stream_reader.h>

#include <memory>
#include <vector>

#include "deep_options.h"

namespace exrio {

// Populate the imagesInfo buffer
int SaveImageInfo(const Options& opts,
                  std::vector<std::unique_ptr<exrio::DeepStreamReader>>& imagesInfo);

// Write the results back to disk.
// Throws std::runtime_error on failure.
void WriteFlatOutputs(const std::vector<float>& flatRgba, const std::string& outputUri,
                      bool flatOutput, bool pngOutput, int width, int height);

}  // namespace exrio
