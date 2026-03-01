#ifndef SKWR_CORE_SPECTRAL_SPECTRUM_H_
#define SKWR_CORE_SPECTRAL_SPECTRUM_H_

#include <algorithm>
#include <array>
#include <cmath>

#include "core/cpu_config.h"

namespace skwr {

template <int NSamples>
struct alignas(16) SpectralPacket {
    static_assert(NSamples > 0);

  public:
    SpectralPacket() {
        for (int i = 0; i < NSamples; ++i) values[i] = 0.0f;
    };
    explicit SpectralPacket(float a) {
        for (int i = 0; i < NSamples; ++i) values[i] = a;
    }

    float operator[](int i) const { return values[i]; }
    float& operator[](int i) { return values[i]; }

    // // Raw Data Access (Needed for IO / OpenEXR)
    // float* data() { return c; }
    // const float* data() const { return c; }

    bool IsBlack() const {
        for (int i = 0; i < NSamples; ++i)
            if (values[i] != 0.f) return false;
        return true;
    };
    bool HasNaNs() const {
        for (int i = 0; i < NSamples; ++i)
            if (std::isnan(values[i])) return true;
        return false;
    }

    SpectralPacket& operator+=(const SpectralPacket& s) {
        for (int i = 0; i < NSamples; ++i) values[i] += s.values[i];
        return *this;
    }
    SpectralPacket& operator-=(const SpectralPacket& s) {
        for (int i = 0; i < NSamples; ++i) {
            values[i] -= s.values[i];
        }
        return *this;
    }
    SpectralPacket& operator*=(const SpectralPacket& s) {
        for (int i = 0; i < NSamples; ++i) {
            values[i] *= s.values[i];
        }
        return *this;
    }
    SpectralPacket& operator*=(float a) {
        for (int i = 0; i < NSamples; ++i) {
            values[i] *= a;
        }
        return *this;
    }
    SpectralPacket& operator/=(const SpectralPacket& s) {
        for (int i = 0; i < NSamples; ++i) {
            values[i] /= s.values[i];
        }
        return *this;
    }
    SpectralPacket& operator/=(float a) {
        for (int i = 0; i < NSamples; ++i) {
            values[i] /= a;
        }
        return *this;
    }

    float MinComponentValue() const {
        float m = values[0];
        for (int i = 1; i < NSamples; ++i) m = std::min(m, values[i]);
        return m;
    }

    float MaxComponentValue() const {
        float m = values[0];
        for (int i = 1; i < NSamples; ++i) m = std::max(m, values[i]);
        return m;
    }

    float Average() const {
        float sum = values[0];
        for (int i = 1; i < NSamples; ++i) sum += values[i];
        return sum / NSamples;
    }

  private:
    std::array<float, NSamples> values;
};

template <int NSamples>
inline SpectralPacket<NSamples> operator+(SpectralPacket<NSamples> s,
                                          const SpectralPacket<NSamples>& c) {
    return s += c;
}

template <int NSamples>
inline SpectralPacket<NSamples> operator-(SpectralPacket<NSamples> s,
                                          const SpectralPacket<NSamples>& c) {
    return s -= c;
}

template <int NSamples>
inline SpectralPacket<NSamples> operator*(SpectralPacket<NSamples> s,
                                          const SpectralPacket<NSamples>& c) {
    return s *= c;
}

template <int NSamples>
inline SpectralPacket<NSamples> operator*(float a, SpectralPacket<NSamples> s) {
    return s *= a;
}

template <int NSamples>
inline SpectralPacket<NSamples> operator*(SpectralPacket<NSamples> s, float a) {
    return s *= a;
}

template <int NSamples>
inline SpectralPacket<NSamples> operator/(SpectralPacket<NSamples> s, float a) {
    return s /= a;
}

template <int N>
struct WavelengthPacket {
    std::array<float, N> lambda;
    std::array<float, N> pdf;
};

using Spectrum = SpectralPacket<kNSamples>;
using SampledWavelengths = WavelengthPacket<kNSamples>;

}  // namespace skwr

#endif  // SKWR_CORE_SPECTRAL_SPECTRUM_H_
