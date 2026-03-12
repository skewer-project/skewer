#ifndef LOOM_SRC_DEEP_MERGER_H
#define LOOM_SRC_DEEP_MERGER_H

#include <utility>
#include <vector>

#include "deep_row.h"

struct RawSample {
    float r, g, b, a, z, z_back;

    // Required for std::sort
    bool operator<(const RawSample& other) const {
        if (z != other.z) return z < other.z;
        return z_back < other.z_back;
    }
};

// Determines if a sample is a volume or a flat plane
bool IsVolume(const RawSample& s);

bool IsNearDepth(const RawSample& a, const RawSample& b, float epsilon);

RawSample BlendCoincidentSamples(const RawSample& current, const RawSample& next);

std::pair<RawSample, RawSample> SplitSample(const RawSample& s, float zSplit);

// Takes raw sample data from multiple input rows for a single pixel, merges them, and writes to the
// output row
void SortAndMergePixelsDirect(int x, const std::vector<const float*>& pixelDataPtrs,
                              const std::vector<unsigned int>& pixelSampleCounts,
                              DeepRow& outputRow);

void SortAndMergePixelsWithSplit(int x, const std::vector<const float*>& pixelDataPtrs,
                                 const std::vector<unsigned int>& pixelSampleCounts,
                                 DeepRow& outputRow);

#endif  // LOOM_SRC_DEEP_MERGER_H
