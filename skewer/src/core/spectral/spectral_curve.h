#ifndef SKWR_CORE_SPECTRAL_SPECTRAL_CURVE_H_
#define SKWR_CORE_SPECTRAL_SPECTRAL_CURVE_H_

namespace skwr {

struct SpectralCurve {
    float coeff[3];
    float scale = 0.0f;
};

}  // namespace skwr

#endif  // SKWR_CORE_SPECTRAL_SPECTRAL_CURVE_H_
