#ifndef SKWR_CORE_SAMPLING_WAVELENGTH_SAMPLER_H_
#define SKWR_CORE_SAMPLING_WAVELENGTH_SAMPLER_H_

#include "core/spectral/spectrum.h"

namespace skwr {

class WavelengthSampler {
  public:
    static constexpr float kLambdaMin = 360.0f;  // TODO: constants?
    static constexpr float kLambdaMax = 830.0f;

    static SampledWavelengths Sample(float u) {
        SampledWavelengths wl;
        const float range = kLambdaMax - kLambdaMin;
        const float pdf = 1.0f / range;
        const float delta = range / kNSamples;

        // The "Hero" wavelength
        wl.lambda[0] = kLambdaMin + u * range;
        wl.pdf[0] = pdf;

        // Stratify the other wavelengths (if kNSamples > 1)
        for (int i = 1; i < kNSamples; ++i) {
            float lambda = wl.lambda[0] + i * delta;
            if (lambda > kLambdaMax) {
                lambda -= range;  // Wrap around if we exceed the visible range
            }
            wl.lambda[i] = lambda;
            wl.pdf[i] = pdf;
        }

        return wl;
    }
};

}  // namespace skwr

#endif  // SKWR_CORE_SAMPLING_WAVELENGTH_SAMPLER_H_
