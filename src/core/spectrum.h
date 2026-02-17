#ifndef SKWR_CORE_SPECTRUM_H_
#define SKWR_CORE_SPECTRUM_H_

#include <iostream>

#include "core/color.h"
#include "core/constants.h"

namespace skwr {

class Spectrum {
  public:
    // Constructors
    Spectrum() : c_{0, 0, 0} {}
    explicit Spectrum(float r, float g, float b) : c_{r, g, b} {}
    explicit Spectrum(float v) : c_{v, v, v} {}

    // Use get<i>(c) because we are treating c like a tuple
    [[nodiscard]] auto R() const -> float { return std::get<0>(c_); }
    [[nodiscard]] auto G() const -> float { return std::get<1>(c_); }
    [[nodiscard]] auto B() const -> float { return std::get<2>(c_); }

    auto operator+=(const Spectrum& v) -> Spectrum& {
        c_[0] += v.c_[0];
        c_[1] += v.c_[1];
        c_[2] += v.c_[2];
        return *this;
    }

    auto operator-=(const Spectrum& v) -> Spectrum& {
        c_[0] -= v.c_[0];
        c_[1] -= v.c_[1];
        c_[2] -= v.c_[2];
        return *this;
    }

    auto operator*=(const Spectrum& v) -> Spectrum& {
        c_[0] *= v.c_[0];
        c_[1] *= v.c_[1];
        c_[2] *= v.c_[2];
        return *this;
    }

    auto operator*=(float t) -> Spectrum& {
        c_[0] *= t;
        c_[1] *= t;
        c_[2] *= t;
        return *this;
    }

    auto operator/=(float t) -> Spectrum& {
        float k = 1.0F / t;
        c_[0] *= k;
        c_[1] *= k;
        c_[2] *= k;
        return *this;
    }

    // Raw Data Access (Needed for IO / OpenEXR)
    [[nodiscard]] auto Data() -> float* { return c_.begin(); }
    [[nodiscard]] auto Data() const -> const Float* { return c_.begin(); }

    [[nodiscard]] auto IsBlack() const -> bool { return c_[0] == 0 && c_[1] == 0 && c_[2] == 0; }
    [[nodiscard]] auto HasNaNs() const -> bool {
        return std::isnan(c_[0]) || std::isnan(c_[1]) || std::isnan(c_[2]);
    }

    // Convert Physics -> Data (For the Film)
    [[nodiscard]] auto ToColor() const -> Color {
        return {std::get<0>(c_), std::get<1>(c_), std::get<2>(c_)};
    }

    // Convert Data -> Physics (For Textures)
    static auto FromColor(const Color& color) -> Spectrum {
        return Spectrum(color.R(), color.G(), color.B());
    }

  private:
    std::array<float, 3> c_;
};

inline auto operator<<(std::ostream& out, const Spectrum& c) -> std::ostream& {
    return out << c.R() << ' ' << c.G() << ' ' << c.B();
}

inline auto operator+(const Spectrum& c, const Spectrum& d) -> Spectrum {
    return Spectrum(c.R() + d.R(), c.G() + d.G(), c.B() + d.B());
}

inline auto operator-(const Spectrum& c, const Spectrum& d) -> Spectrum {
    return Spectrum(c.R() - d.R(), c.G() - d.G(), c.B() - d.B());
}

inline auto operator*(const Spectrum& c, const Spectrum& d) -> Spectrum {
    return Spectrum(c.R() * d.R(), c.G() * d.G(), c.B() * d.B());
}

inline auto operator*(Float t, const Spectrum& c) -> Spectrum {
    return Spectrum(t * c.R(), t * c.G(), t * c.B());
}

inline auto operator*(const Spectrum& c, Float t) -> Spectrum { return t * c; }

inline auto operator/(const Spectrum& c, Float t) -> Spectrum { return c * (1.0F / t); }

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
