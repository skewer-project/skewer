#ifndef SKWR_CORE_SPECTRUM_H_
#define SKWR_CORE_SPECTRUM_H_

#include <iostream>

#include "core/color.h"
#include "core/constants.h"

namespace skwr {

class Spectrum {
  public:
    // Constructors
    Spectrum() : c{0, 0, 0} {}
    explicit Spectrum(Float r, Float g, Float b) : c{r, g, b} {}
    explicit Spectrum(Float v) : c{v, v, v} {}

    Float r() const { return c[0]; }
    Float g() const { return c[1]; }
    Float b() const { return c[2]; }

    Spectrum &operator+=(const Spectrum &v) {
        c[0] += v.c[0];
        c[1] += v.c[1];
        c[2] += v.c[2];
        return *this;
    }

    Spectrum &operator-=(const Spectrum &v) {
        c[0] -= v.c[0];
        c[1] -= v.c[1];
        c[2] -= v.c[2];
        return *this;
    }

    Spectrum &operator*=(const Spectrum &v) {
        c[0] *= v.c[0];
        c[1] *= v.c[1];
        c[2] *= v.c[2];
        return *this;
    }

    Spectrum &operator*=(Float t) {
        c[0] *= t;
        c[1] *= t;
        c[2] *= t;
        return *this;
    }

    Spectrum &operator/=(Float t) {
        Float k = 1.0 / t;
        c[0] *= k;
        c[1] *= k;
        c[2] *= k;
        return *this;
    }

    bool IsBlack() const { return c[0] == 0 && c[1] == 0 && c[2] == 0; }
    bool HasNaNs() const { return std::isnan(c[0]) || std::isnan(c[1]) || std::isnan(c[2]); }

    // Convert Physics -> Data (For the Film)
    Color ToColor() const { return Color(c[0], c[1], c[2]); }

    // Convert Data -> Physics (For Textures)
    static Spectrum FromColor(const Color &color) {
        return Spectrum(color.r(), color.g(), color.b());
    }

  private:
    Float c[3];
};

inline std::ostream &operator<<(std::ostream &out, const Spectrum &c) {
    return out << c.r() << ' ' << c.g() << ' ' << c.b();
}

inline Spectrum operator+(const Spectrum &c, const Spectrum &d) {
    return Spectrum(c.r() + d.r(), c.g() + d.g(), c.b() + d.b());
}

inline Spectrum operator-(const Spectrum &c, const Spectrum &d) {
    return Spectrum(c.r() - d.r(), c.g() - d.g(), c.b() - d.b());
}

inline Spectrum operator*(const Spectrum &c, const Spectrum &d) {
    return Spectrum(c.r() * d.r(), c.g() * d.g(), c.b() * d.b());
}

inline Spectrum operator*(Float t, const Spectrum &c) {
    return Spectrum(t * c.r(), t * c.g(), t * c.b());
}

inline Spectrum operator*(const Spectrum &c, Float t) { return t * c; }

inline Spectrum operator/(const Spectrum &c, Float t) { return c * (1.0 / t); }

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
