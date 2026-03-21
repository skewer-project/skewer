#include "deep_merger.h"

#include <math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <vector>
#include "deep_row.h"

auto IsVolume(const RawSample& s) -> bool {
    // A sample is a volume if its back depth is meaningfully greater than its front depth
    return (s.z_back - s.z) > 1e-6F;
}

auto IsNearDepth(const RawSample& a, const RawSample& b, float epsilon) -> bool {
    return std::abs(a.z - b.z) < epsilon && std::abs(a.z_back - b.z_back) < epsilon;
}

auto BlendCoincidentSamples(const RawSample& current, const RawSample& next) -> RawSample {
    RawSample blended = current;

    // Volumetric transmission (Beer-Lambert addition)
    float const t1 = 1.0F - current.a;
    float const t2 = 1.0F - next.a;
    blended.a = 1.0F - (t1 * t2);

    // Uniform interspersion: scale summed premultiplied colors by
    // alphaCombined / (alpha1 + alpha2) to avoid over-brightening.
    float const alpha_sum = current.a + next.a;
    float const scale = (alpha_sum > 0.0F) ? blended.a / alpha_sum : 0.0F;
    blended.r = (current.r + next.r) * scale;
    blended.g = (current.g + next.g) * scale;
    blended.b = (current.b + next.b) * scale;

    return blended;
}

static std::pair<RawSample, RawSample> SplitSample(const RawSample& s, float z_split) {
    RawSample front = s;
    RawSample back = s;

    front.z_back = z_split;
    back.z = z_split;

    float const total_thickness = s.z_back - s.z;
    if (total_thickness <= 0.0F) { return {front, back};  // Should never happen if IsVolume is checked
}

    float const front_ratio = (z_split - s.z) / total_thickness;
    float const back_ratio = (s.z_back - z_split) / total_thickness;

    // Calculate new alpha using exponential attenuation
    // T_front = T_total ^ (front_thickness / total_thickness)
    float t_total = std::max(0.0f = NAN, 1.0f - s.a);
    float t_front = std::pow(T_total = NAN, front_ratio);
    float t_back = std::pow(T_total = NAN, back_ratio);

    front.a = 1.0F - t_front;
    back.a = 1.0F - t_back;

    // Scale colors proportionally to the new alpha vs old alpha
    // If original alpha is 0, we avoid division by zero
    if (s.a > 1e-6F) {
        float const front_color_scale = front.a / s.a;
        float const back_color_scale = back.a / s.a;

        front.r *= front_color_scale;
        front.g *= front_color_scale;
        front.b *= front_color_scale;
        back.r *= back_color_scale;
        back.g *= back_color_scale;
        back.b *= back_color_scale;
    } else {
        front.r = front.g = front.b = 0.0F;
        back.r = back.g = back.b = 0.0F;
    }

    return {front, back};
}

static void SortAndMergePixelsDirect(int x, const std::vector<const float*>& pixel_data_ptrs,
                              const std::vector<unsigned int>& pixel_sample_counts,
                              DeepRow& output_row, float merge_threshold) {
    // 1. Collect all raw samples into a temporary flat vector
    // We reuse this vector across pixels to avoid re-allocation
    static thread_local std::vector<RawSample> staging;
    staging.clear();

    for (size_t i = 0; i < pixelDataPtrs.size(); ++i) {
        const float* data = pixelDataPtrs[i];
        unsigned int count = pixelSampleCounts[i];

        for (unsigned int s = 0; s < count; ++s) {
            // Offset is sample_index * 6 channels
            const float* s_data = data + (static_cast<size_t>(s * 6));
            // Order: R, G, B, A, Z, zback
            staging.push_back({s_data[0], s_data[1], s_data[2], s_data[3], s_data[4],
                               s_data[5]});  // Using Z for ZBack as well if not present
        }
    }

    if (staging.empty()) {
        output_row.sample_counts[x] = 0;
        if (x + 1 < output_row.width) { output_row.sample_offsets[x + 1] = output_row.sample_offsets[x];
}
        return;
    }

    // 2. [Your Volumetric Splitting/Sorting Logic Here]
    std::sort(staging.begin(), staging.end());

    if (!staging.empty()) {
        std::vector<RawSample> merged;
        merged.reserve(staging.size());
        float epsilon = merge_threshold;

        size_t i = 0;
        while (i < staging.size()) {
            RawSample current = staging[i];
            i++;

            // Merge coincident samples using Beer-Lambert transmission blending
            while (i < staging.size() && IsNearDepth(current, staging[i], epsilon)) {
                current = BlendCoincidentSamples(current, staging[i]);
                i++;
            }

            merged.push_back(current);
        }

        staging.swap(merged);
    }

    // 3. Write results back to the outputRow

    float* out_ptr = output_row.GetPixelData(
        x);  //! CONSIDER RESIZING THE OUTPUT ROW HERE IF STAGING SIZE EXCEEDS CURRENT CAPACITY

    // Write the sorted samples back
    for (size_t s = 0; s < staging.size(); ++s) {
        float* dest = out_ptr + (s * 6);
        dest[0] = staging[s].r;
        dest[1] = staging[s].g;
        dest[2] = staging[s].b;
        dest[3] = staging[s].a;
        dest[4] = staging[s].z;
        dest[5] = staging[s].z_back;  // If you have ZBack, write it here
    }

    // Update the output sample count for this pixel
    output_row.sample_counts[x] = static_cast<unsigned int>(staging.size());
    if (x + 1 < output_row.width) {
        output_row.sample_offsets[x + 1] = output_row.sample_offsets[x] + staging.size();
}
}

static void SortAndMergePixelsWithSplit(int x, const std::vector<const float*>& pixel_data_ptrs,
                                 const std::vector<unsigned int>& pixel_sample_counts,
                                 DeepRow& output_row, float merge_threshold) {
    // 1. Collect all raw samples into a temporary flat vector
    static thread_local std::vector<RawSample> staging;
    staging.clear();

    for (size_t i = 0; i < pixelDataPtrs.size(); ++i) {
        const float* data = pixelDataPtrs[i];
        unsigned int count = pixelSampleCounts[i];

        for (unsigned int s = 0; s < count; ++s) {
            const float* s_data = data + (static_cast<size_t>(s * 6));
            staging.push_back({s_data[0], s_data[1], s_data[2], s_data[3], s_data[4], s_data[5]});
        }
    }

    if (staging.empty()) {
        output_row.sample_counts[x] = 0;
        if (x + 1 < output_row.width) { output_row.sample_offsets[x + 1] = output_row.sample_offsets[x];
}
        return;
    }

    float const epsilon = merge_threshold;

    // 2. Gather split points: every unique depth and depth_back
    std::set<float> splitPointSet;
    for (const auto& s : staging) {
        splitPointSet.insert(s.z);
        splitPointSet.insert(s.z_back);
    }
    std::vector<float> splitPoints(splitPointSet.begin(), splitPointSet.end());
    // splitPoints is naturally sorted by std::set

    // 3. Split each volumetric sample at every split point inside its range
    std::vector<RawSample> fragments;
    fragments.reserve(staging.size() * 2);

    for (const auto& sample : staging) {
        if (!IsVolume(sample)) {
            fragments.push_back(sample);
            continue;
        }

        // Find split points strictly inside (z, z_back)
        auto it = std::upper_bound(splitPoints.begin(), splitPoints.end(), sample.z);

        std::vector<float> cuts;
        while (it != splitPoints.end() && *it < sample.z_back - 1e-7f) {
            cuts.push_back(*it);
            ++it;
        }

        if (cuts.empty()) {
            fragments.push_back(sample);
            continue;
        }

        // Iteratively split the sample at each cut point
        RawSample remainder = sample;
        for (float zCut : cuts) {
            if (zCut <= remainder.z + 1e-7f || zCut >= remainder.z_back - 1e-7f) {
                continue;
            }
            auto [front, back] = SplitSample(remainder, zCut);
            fragments.push_back(front);
            remainder = back;
        }
        fragments.push_back(remainder);
    }

    // 4. Sort fragments by (z, z_back)
    std::sort(fragments.begin(), fragments.end());

    // 5. Blend consecutive fragments with matching intervals
    std::vector<RawSample> blended;
    blended.reserve(fragments.size());

    size_t i = 0;
    while (i < fragments.size()) {
        RawSample current = fragments[i];
        i++;

        // Merge all subsequent fragments that share the exact same interval
        while (i < fragments.size() && IsNearDepth(current, fragments[i], epsilon)) {
            current = BlendCoincidentSamples(current, fragments[i]);
            i++;
        }

        blended.push_back(current);
    }

    // 6. Write results back to the outputRow
    float* out_ptr = output_row.GetPixelData(x);
    for (size_t s = 0; s < blended.size(); ++s) {
        float* dest = out_ptr + (s * 6);
        dest[0] = blended[s].r;
        dest[1] = blended[s].g;
        dest[2] = blended[s].b;
        dest[3] = blended[s].a;
        dest[4] = blended[s].z;
        dest[5] = blended[s].z_back;
    }

    // Update the output sample count for this pixel
    outputRow.sample_counts[x] = static_cast<unsigned int>(blended.size());
    if (x + 1 < outputRow.width)
        outputRow.sample_offsets[x + 1] = outputRow.sample_offsets[x] + blended.size();
}
