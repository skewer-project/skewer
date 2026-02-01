#ifndef SKWR_CORE_COLOR_H_
#define SKWR_CORE_COLOR_H_

#include <algorithm>
#include <cmath>
#include <iostream>

#include "core/constants.h"

namespace skwr {

class Color {
  public:
    Color() : c{0, 0, 0} {}
    Color(Float r, Float g, Float b) : c{r, g, b} {}
    explicit Color(Float v) : c{v, v, v} {}
    // explicit Color(double r, double g, double b)
    //     : r(static_cast<Float>(r)), g(static_cast<Float>(g)), b(static_cast<Float>(b)) {}
    // Color(int r, int g, int b)
    //     : r(static_cast<Float>(r)), g(static_cast<Float>(g)), b(static_cast<Float>(b)) {}

    Float r() const { return c[0]; }
    Float g() const { return c[1]; }
    Float b() const { return c[2]; }

    // Array Access (for loops)
    Float operator[](int i) const { return c[i]; }
    Float &operator[](int i) { return c[i]; }

    // Float operator[](int i) const {
    //     if (i == 0) return r;
    //     if (i == 1) return g;
    //     return b;
    // }

    // // reference version for assignment
    // Float &operator[](int i) {
    //     if (i == 0) return r;
    //     if (i == 1) return g;
    //     return b;
    // }

    Color &operator+=(const Color &v) {
        c[0] += v.c[0];
        c[1] += v.c[1];
        c[2] += v.c[2];
        return *this;
    }

    Color &operator*=(Float t) {
        c[0] *= t;
        c[1] *= t;
        c[2] *= t;
        return *this;
    }

    Color operator/(Float t) const {
        Float inv = 1.0f / t;
        return Color(c[0] * inv, c[1] * inv, c[2] * inv);
    }

    void applygammacorrection() {
        auto lineartogamma = [](Float x) { return (x > 0) ? std::sqrt(x) : 0; };
        c[0] = lineartogamma(c[0]);
        c[1] = lineartogamma(c[1]);
        c[2] = lineartogamma(c[2]);
    }

    // Clamp helper
    Color clamp(Float min = 0.0f, Float max = 1.0f) const {
        return Color(std::clamp(c[0], min, max), std::clamp(c[1], min, max),
                     std::clamp(c[2], min, max));
    }
    // Manual version if not C++17
    /*
    Color clamped(Float min = 0.0f, Float max = 1.0f) const {
        auto c = [](Float val, Float low, Float high) {
            return val < low ? low : (val > high ? high : val);
        };
        return Color(c(r, min, max), c(g, min, max), c(b, min, max));
    }
    */

    bool HasNaNs() const { return std::isnan(c[0]) || std::isnan(c[1]) || std::isnan(c[2]); }

  private:
    Float c[3];
};

inline std::ostream &operator<<(std::ostream &out, const Color &c) {
    return out << c.r() << ' ' << c.g() << ' ' << c.b();
}

inline Color operator+(const Color &c, const Color &d) {
    return Color(c.r() + d.r(), c.g() + d.g(), c.b() + d.b());
}

inline Color operator-(const Color &c, const Color &d) {
    return Color(c.r() - d.r(), c.g() - d.g(), c.b() - d.b());
}

inline Color operator*(const Color &c, const Color &d) {
    return Color(c.r() * d.r(), c.g() * d.g(), c.b() * d.b());
}

inline Color operator*(Float t, const Color &c) { return Color(t * c.r(), t * c.g(), t * c.b()); }

inline Color operator*(const Color &c, Float t) { return t * c; }

inline Color operator/(const Color &c, Float t) { return c * (1.0 / t); }

}  // namespace skwr

#endif
