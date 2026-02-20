#ifndef SKWR_CORE_COLOR_H_
#define SKWR_CORE_COLOR_H_

#include <algorithm>
#include <cmath>
#include <iostream>

namespace skwr {

struct RGB {
  public:
    RGB() : c{0, 0, 0} {}
    RGB(float r, float g, float b) : c{r, g, b} {}
    explicit RGB(float v) : c{v, v, v} {}

    float r() const { return c[0]; }
    float g() const { return c[1]; }
    float b() const { return c[2]; }

    float operator[](int i) const { return c[i]; }
    float& operator[](int i) { return c[i]; }

    RGB& operator+=(const RGB& v) {
        c[0] += v.c[0];
        c[1] += v.c[1];
        c[2] += v.c[2];
        return *this;
    }

    RGB& operator*=(float t) {
        c[0] *= t;
        c[1] *= t;
        c[2] *= t;
        return *this;
    }

    RGB LinearToSRGB(const RGB&);
    RGB ToneMap(const RGB&);
    float Luminance() const {
        // Rec.709
        return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    }

    bool HasNaNs() const { return std::isnan(c[0]) || std::isnan(c[1]) || std::isnan(c[2]); }
    bool IsBlack() const { return c[0] == 0 && c[1] == 0 && c[2] == 0; }
    bool IsFinite() const {
        return std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]);
    }

    // Clamp helper
    RGB Clamp(float min = 0.0f, float max = 1.0f) const {
        return RGB(std::clamp(c[0], min, max), std::clamp(c[1], min, max),
                   std::clamp(c[2], min, max));
    }

  private:
    float c[3];
};

inline std::ostream& operator<<(std::ostream& out, const RGB& c) {
    return out << c.r() << ' ' << c.g() << ' ' << c.b();
}

inline RGB operator+(const RGB& c, const RGB& d) {
    return RGB(c.r() + d.r(), c.g() + d.g(), c.b() + d.b());
}

inline RGB operator-(const RGB& c, const RGB& d) {
    return RGB(c.r() - d.r(), c.g() - d.g(), c.b() - d.b());
}

inline RGB operator*(const RGB& c, const RGB& d) {
    return RGB(c.r() * d.r(), c.g() * d.g(), c.b() * d.b());
}

inline RGB operator*(float t, const RGB& c) { return RGB(t * c.r(), t * c.g(), t * c.b()); }

inline RGB operator*(const RGB& c, float t) { return t * c; }

inline RGB operator/(const RGB& c, float t) { return c * (1.0 / t); }

inline float ToLinear(float x) {
    if (x <= 0.04045f) return x / 12.92f;
    return std::pow((x + 0.055f) / 1.055f, 2.4f);
}

inline RGB ToLinear(const RGB& c) { return RGB(ToLinear(c.r()), ToLinear(c.g()), ToLinear(c.b())); }

}  // namespace skwr

#endif  // SKWR_CORE_COLOR_H_
