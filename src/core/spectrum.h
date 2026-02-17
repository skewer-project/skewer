#ifndef SKWR_CORE_SPECTRUM_H_
#define SKWR_CORE_SPECTRUM_H_

#include <algorithm>
#include <iostream>

#include "core/color.h"

namespace skwr {

// ex refactor
// template<int N>
// class Spectrum {
// public:
//     static constexpr int Size() { return N; }

//     Float MaxComponent() const {
//         return *std::max_element(c.begin(), c.end());
//     }

//     Float MinComponent() const {
//         return *std::min_element(c.begin(), c.end());
//     }

//     Float Average() const {
//         return std::accumulate(c.begin(), c.end(), Float(0)) / N;
//     }

//     Float& operator[](int i)       { return c[i]; }
//     Float  operator[](int i) const { return c[i]; }

// private:
//     std::array<Float, N> c;
// };

class Spectrum {
  public:
    // Constructors
    Spectrum() : c{0, 0, 0} {}
    explicit Spectrum(float r, float g, float b) : c{r, g, b} {}
    explicit Spectrum(float v) : c{v, v, v} {}

    float r() const { return c[0]; }
    float g() const { return c[1]; }
    float b() const { return c[2]; }

    Spectrum& operator+=(const Spectrum& v) {
        c[0] += v.c[0];
        c[1] += v.c[1];
        c[2] += v.c[2];
        return *this;
    }

    Spectrum& operator-=(const Spectrum& v) {
        c[0] -= v.c[0];
        c[1] -= v.c[1];
        c[2] -= v.c[2];
        return *this;
    }

    Spectrum& operator*=(const Spectrum& v) {
        c[0] *= v.c[0];
        c[1] *= v.c[1];
        c[2] *= v.c[2];
        return *this;
    }

    Spectrum& operator*=(float t) {
        c[0] *= t;
        c[1] *= t;
        c[2] *= t;
        return *this;
    }

    Spectrum& operator/=(float t) {
        float k = 1.0 / t;
        c[0] *= k;
        c[1] *= k;
        c[2] *= k;
        return *this;
    }

    // Raw Data Access (Needed for IO / OpenEXR)
    float* data() { return c; }
    const float* data() const { return c; }

    bool IsBlack() const { return c[0] == 0 && c[1] == 0 && c[2] == 0; }
    bool HasNaNs() const { return std::isnan(c[0]) || std::isnan(c[1]) || std::isnan(c[2]); }

    // Convert Physics -> Data (For the Film)
    Color ToColor() const { return Color(c[0], c[1], c[2]); }

    // Convert Data -> Physics (For Textures)
    static Spectrum FromColor(const Color& color) {
        return Spectrum(color.r(), color.g(), color.b());
    }

    /**
     * TODO: When refactoring spectrum, a lot of this will change
     * These are just temporarily slap-on fixes
     * template<int N>
        class Spectrum {
        public:
            static constexpr int Size() { return N; }
            Float MaxComponent() const {
                return *std::max_element(c.begin(), c.end());
            }

            Float MinComponent() const {
                return *std::min_element(c.begin(), c.end());
            }
        private:
            Float c[N];
        };
     */
    int Size() const { return sizeof(c) / sizeof(c[0]); }
    float MaxComponent() const { return *std::max_element(c, c + Size()); }
    float MinComponent() const { return *std::min_element(c, c + Size()); }
    float Average() const {
        float sum = 0.0f;
        for (int i = 0; i < Size(); ++i) {
            sum += c[i];
        }
        return sum / Size();
    }

  private:
    float c[3];
};

inline std::ostream& operator<<(std::ostream& out, const Spectrum& c) {
    return out << c.r() << ' ' << c.g() << ' ' << c.b();
}

inline Spectrum operator+(const Spectrum& c, const Spectrum& d) {
    return Spectrum(c.r() + d.r(), c.g() + d.g(), c.b() + d.b());
}

inline Spectrum operator-(const Spectrum& c, const Spectrum& d) {
    return Spectrum(c.r() - d.r(), c.g() - d.g(), c.b() - d.b());
}

inline Spectrum operator*(const Spectrum& c, const Spectrum& d) {
    return Spectrum(c.r() * d.r(), c.g() * d.g(), c.b() * d.b());
}

inline Spectrum operator*(float t, const Spectrum& c) {
    return Spectrum(t * c.r(), t * c.g(), t * c.b());
}

inline Spectrum operator*(const Spectrum& c, float t) { return t * c; }

inline Spectrum operator/(const Spectrum& c, float t) { return c * (1.0 / t); }

// void write_color(std::ostream &out, const Spectrum &pixel_color)
// {
//     if (std::isnan(pixel_color.r))
//         return;

//     // Copy so we can gamma correct
//     Spectrum c = pixel_color;

//     c.applygammacorrection();

//     // Translate [0,1] component values to rgb range [0,255]
//     c.clamp(0.0f, 0.999f);

//     // since adding average of all samples, need to clamp values to prevent going
//     // beyond [0,1] range

//     // Write out pixel components
//     out << static_cast<int>(256 * c.r) << ' ' << static_cast<int>(256 * c.g) << ' ' <<
//     static_cast<int>(256 * c.b) << '\n';
// }
}  // namespace skwr

#endif  // SKWR_CORE_SPECTRUM_H_
