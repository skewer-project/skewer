#pragma once

#include <exrio/deep_image.h>

#include <utility>
#include <vector>

namespace deep_compositor {

/**
 * Split a volumetric sample at an interior depth using Beer-Lambert
 * exponential attenuation.
 *
 * Invariant: (1 - front.alpha) * (1 - back.alpha) == (1 - sample.alpha)
 *
 * If z_split is not strictly inside the sample's range, returns the
 * original sample unchanged in the first element and a zero sample
 * in the second.
 */
std::pair<DeepSample, DeepSample> splitSample(const DeepSample& sample, float z_split);

/**
 * Blend two coincident samples that occupy the same [z, z_back] interval.
 * Uses the standard deep compositing formula (uniform interspersion).
 */
DeepSample blendCoincidentSamples(const DeepSample& a, const DeepSample& b);

/**
 * Volumetric merge of multiple deep pixels.
 *
 * 1. Collects all samples from all input pixels
 * 2. Gathers split points (every unique depth and depth_back)
 * 3. Splits volumetric samples at every interior split point (Beer-Lambert)
 * 4. Sorts fragments by (depth, depth_back)
 * 5. Blends coincident fragments sharing the same interval
 *
 * The result is a single DeepPixel with non-overlapping, sorted intervals
 * ready for front-to-back Over compositing.
 */
DeepPixel mergePixelsVolumetric(const std::vector<const DeepPixel*>& pixels,
                                float epsilon = 0.001f);

}  // namespace deep_compositor
