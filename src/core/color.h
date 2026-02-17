#ifndef SKWR_CORE_COLOR_H_
#define SKWR_CORE_COLOR_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

#include "core/constants.h"

namespace skwr {

class Color {
  public:
    Color() : c_{0, 0, 0} {}
    Color(float r, float g, float b) : c_{r, g, b} {}
    explicit Color(float v) : c_{v, v, v} {}

    // Use get<i>(c) because we are treating c like a tuple
    [[nodiscard]] auto R() const -> float { return std::get<0>(c_); }
    [[nodiscard]] auto G() const -> float { return std::get<1>(c_); }
    [[nodiscard]] auto B() const -> float { return std::get<2>(c_); }

    // Array Access (for loops)
    auto operator[](int i) const -> float { return c_.at(i); }
    auto operator[](int i) -> float& {
        assert(i >= 0 && i < 3);
        return c_.at(i);
    }

    auto operator+=(const Color& v) -> Color& {
        c_[0] += v.c_[0];
        c_[1] += v.c_[1];
        c_[2] += v.c_[2];
        return *this;
    }

    auto operator*=(Float t) -> Color& {
        c_[0] *= t;
        c_[1] *= t;
        c_[2] *= t;
        return *this;
    }

    auto operator/(Float t) const -> Color {
        Float inv = 1.0F / t;
        return {c_[0] * inv, c_[1] * inv, c_[2] * inv};
    }

    void ApplyGammaCorrection() {
        auto lineartogamma = [](Float x) { return (x > 0) ? std::sqrt(x) : 0; };
        c_[0] = lineartogamma(c_[0]);
        c_[1] = lineartogamma(c_[1]);
        c_[2] = lineartogamma(c_[2]);
    }

    // Clamp helper
    [[nodiscard]] auto Clamp(Float min = 0.0F, Float max = 1.0F) const -> Color {
        return {std::clamp(c_[0], min, max), std::clamp(c_[1], min, max),
                std::clamp(c_[2], min, max)};
    }
    // Manual version if not C++17
    /*
    Color Clamped(Float min = 0.0f, Float max = 1.0f) const {
        auto c = [](Float val, Float low, Float high) {
            return val < low ? low : (val > high ? high : val);
        };
        return Color(c(r, min, max), c(g, min, max), c(b, min, max));
    }
    */

    [[nodiscard]] auto HasNaNs() const -> bool {
        return std::isnan(c_[0]) || std::isnan(c_[1]) || std::isnan(c_[2]);
    }

  private:
    std::array<float, 3> c_;
};

inline auto operator<<(std::ostream& out, const Color& c) -> std::ostream& {
    return out << c.R() << ' ' << c.G() << ' ' << c.B();
}

inline auto operator+(const Color& c, const Color& d) -> Color {
    return {c.R() + d.R(), c.G() + d.G(), c.B() + d.B()};
}

inline auto operator-(const Color& c, const Color& d) -> Color {
    return {c.R() - d.R(), c.G() - d.G(), c.B() - d.B()};
}

inline auto operator*(const Color& c, const Color& d) -> Color {
    return {c.R() * d.R(), c.G() * d.G(), c.B() * d.B()};
}

inline auto operator*(Float t, const Color& c) -> Color {
    return {t * c.R(), t * c.G(), t * c.B()};
}

inline auto operator*(const Color& c, Float t) -> Color { return t * c; }

inline auto operator/(const Color& c, Float t) -> Color { return c * (1.0F / t); }

}  // namespace skwr

#endif
