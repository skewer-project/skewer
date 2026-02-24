#include <exrio/deep_volume.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace deep_compositor {

// ============================================================================
// splitSample -- Beer-Lambert exponential attenuation
// ============================================================================

std::pair<DeepSample, DeepSample> splitSample(const DeepSample& sample, float z_split) {
    constexpr float kEpsilon = 1e-7f;

    float thick = sample.thickness();

    // Not a volume, or split point not strictly inside the range -> no split
    if (thick < kEpsilon || z_split <= sample.depth + kEpsilon || z_split >= sample.depth_back - kEpsilon) {
        DeepSample zero;
        return {sample, zero};
    }

    float alpha = sample.alpha;

    // Clamp alpha to avoid log(0)
    if (alpha >= 1.0f) alpha = 1.0f - kEpsilon;

    float frontThick = z_split - sample.depth;
    float backThick  = sample.depth_back - z_split;

    float alphaFront, alphaBack;

    if (alpha <= 0.0f) {
        // Fully transparent -- two zero-alpha fragments
        alphaFront = 0.0f;
        alphaBack  = 0.0f;
    } else {
        // sigma = -ln(1 - alpha) / thickness
        float sigma = -std::log(1.0f - alpha) / thick;
        alphaFront = 1.0f - std::exp(-sigma * frontThick);
        alphaBack  = 1.0f - std::exp(-sigma * backThick);
    }

    // Premultiplied RGB scales with alpha ratio
    float ratioFront = (alpha > 0.0f) ? alphaFront / alpha : 0.0f;
    float ratioBack  = (alpha > 0.0f) ? alphaBack  / alpha : 0.0f;

    DeepSample front;
    front.depth      = sample.depth;
    front.depth_back = z_split;
    front.red        = sample.red * ratioFront;
    front.green      = sample.green * ratioFront;
    front.blue       = sample.blue * ratioFront;
    front.alpha      = alphaFront;

    DeepSample back;
    back.depth      = z_split;
    back.depth_back = sample.depth_back;
    back.red        = sample.red * ratioBack;
    back.green      = sample.green * ratioBack;
    back.blue       = sample.blue * ratioBack;
    back.alpha      = alphaBack;

    return {front, back};
}

// ============================================================================
// blendCoincidentSamples -- uniform interspersion
// ============================================================================

DeepSample blendCoincidentSamples(const DeepSample& a, const DeepSample& b) {
    // alpha_combined = 1 - (1 - a.alpha)(1 - b.alpha)
    float alphaCombined = a.alpha + b.alpha - a.alpha * b.alpha;

    float alphaSum = a.alpha + b.alpha;
    float scale = (alphaSum > 0.0f) ? alphaCombined / alphaSum : 0.0f;

    DeepSample result;
    result.depth      = a.depth;
    result.depth_back = a.depth_back;
    result.red        = (a.red + b.red) * scale;
    result.green      = (a.green + b.green) * scale;
    result.blue       = (a.blue + b.blue) * scale;
    result.alpha      = alphaCombined;

    return result;
}

// ============================================================================
// mergePixelsVolumetric -- main volumetric merge algorithm
// ============================================================================

DeepPixel mergePixelsVolumetric(const std::vector<const DeepPixel*>& pixels,
                                float epsilon) {
    DeepPixel result;

    // 1. Collect all samples
    size_t totalSamples = 0;
    for (const auto* pixel : pixels) {
        totalSamples += pixel->sampleCount();
    }
    if (totalSamples == 0) return result;

    std::vector<DeepSample> allSamples;
    allSamples.reserve(totalSamples);
    for (const auto* pixel : pixels) {
        for (const auto& s : pixel->samples()) {
            allSamples.push_back(s);
        }
    }

    // 2. Gather split points: every unique depth and depth_back
    std::set<float> splitPointSet;
    for (const auto& s : allSamples) {
        splitPointSet.insert(s.depth);
        splitPointSet.insert(s.depth_back);
    }
    std::vector<float> splitPoints(splitPointSet.begin(), splitPointSet.end());
    // Already sorted by std::set

    // 3. Split each volumetric sample at every split point inside its range
    std::vector<DeepSample> fragments;
    fragments.reserve(allSamples.size() * 2); // rough estimate

    for (const auto& sample : allSamples) {
        if (!sample.isVolume()) {
            // Point / hard-surface sample -- never split
            fragments.push_back(sample);
            continue;
        }

        // Find split points strictly inside (depth, depth_back)
        // Use lower_bound on depth + epsilon to skip the front boundary
        auto it = std::upper_bound(splitPoints.begin(), splitPoints.end(),
                                   sample.depth);

        // Collect interior split points
        std::vector<float> cuts;
        while (it != splitPoints.end() && *it < sample.depth_back - 1e-7f) {
            cuts.push_back(*it);
            ++it;
        }

        if (cuts.empty()) {
            fragments.push_back(sample);
            continue;
        }

        // Iteratively split the sample at each cut point
        DeepSample remainder = sample;
        for (float z : cuts) {
            if (z <= remainder.depth + 1e-7f || z >= remainder.depth_back - 1e-7f) {
                continue;
            }
            auto [front, back] = splitSample(remainder, z);
            fragments.push_back(front);
            remainder = back;
        }
        fragments.push_back(remainder);
    }

    // 4. Sort fragments by (depth, depth_back)
    std::sort(fragments.begin(), fragments.end());

    // 5. Blend consecutive fragments with matching intervals
    std::vector<DeepSample> blended;
    blended.reserve(fragments.size());

    size_t i = 0;
    while (i < fragments.size()) {
        DeepSample current = fragments[i];
        i++;

        // Merge all subsequent fragments that share the same interval
        while (i < fragments.size() && current.isNearDepth(fragments[i], epsilon)) {
            current = blendCoincidentSamples(current, fragments[i]);
            i++;
        }

        blended.push_back(current);
    }

    result.samples() = std::move(blended);
    return result;
}

} // namespace deep_compositor
