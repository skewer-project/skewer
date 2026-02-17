#ifndef SKWR_CORE_VEC3_H_
#define SKWR_CORE_VEC3_H_

#include <cmath>
#include <iostream>

#include "core/constants.h"

namespace skwr {

class Vec3 {
  public:
    Vec3() : e_{0, 0, 0} {}
    Vec3(float e0, float e1, float e2) : e_{e0, e1, e2} {}

    [[nodiscard]] auto X() const -> float { return std::get<0>(e_); }
    [[nodiscard]] auto Y() const -> float { return std::get<1>(e_); }
    [[nodiscard]] auto Z() const -> float { return std::get<2>(e_); }

    auto operator-() const -> Vec3 {
        return {-std::get<0>(e_), -std::get<1>(e_), -std::get<2>(e_)};
    }
    auto operator[](int i) const -> float { return e_.at(i); }
    auto operator[](int i) -> float& { return e_.at(i); }

    auto operator+=(const Vec3& v) -> Vec3& {
        e_[0] += v.e_[0];
        e_[1] += v.e_[1];
        e_[2] += v.e_[2];
        return *this;
    }

    auto operator*=(float t) -> Vec3& {
        e_[0] *= t;
        e_[1] *= t;
        e_[2] *= t;
        return *this;
    }

    auto operator/=(float t) -> Vec3& { return *this *= 1 / t; }

    // Utility Member Functions
    [[nodiscard]] auto Length() const -> float { return std::sqrt(LengthSquared()); }
    [[nodiscard]] auto LengthSquared() const -> float {
        return (std::get<0>(e_) * std::get<0>(e_)) + (std::get<1>(e_) * std::get<1>(e_)) +
               (std::get<2>(e_) * std::get<2>(e_));
    }

    // Return true if the vector is close to zero in all dimensions
    [[nodiscard]] auto NearZero() const -> bool {
        auto s = kMinVal;
        return (std::fabs(e_[0]) < s) && (std::fabs(e_[1]) < s) && (std::fabs(e_[2]) < s);
    }

    // Friend declarations for external operators (optional, for performance or convenience)
    friend auto operator<<(std::ostream& out, const Vec3& v) -> std::ostream&;
    friend auto operator+(const Vec3& u, const Vec3& v) -> Vec3;
    friend auto operator-(const Vec3& u, const Vec3& v) -> Vec3;
    friend auto operator*(const Vec3& u, const Vec3& v) -> Vec3;
    friend auto operator*(Float t, const Vec3& v) -> Vec3;
    friend auto operator*(const Vec3& v, Float t) -> Vec3;
    friend auto operator/(const Vec3& v, Float t) -> Vec3;
    friend auto Dot(const Vec3& u, const Vec3& v) -> float;
    friend auto Cross(const Vec3& u, const Vec3& v) -> Vec3;

  private:
    std::array<float, 3> e_;
};

// point alias for Vec3
using Point3 = Vec3;

// Vector Utility Functions
inline auto operator<<(std::ostream& out, const Vec3& v) -> std::ostream& {
    return out << v.e_[0] << ' ' << v.e_[1] << ' ' << v.e_[2];
}

inline auto operator+(const Vec3& u, const Vec3& v) -> Vec3 {
    return {u.e_[0] + v.e_[0], u.e_[1] + v.e_[1], u.e_[2] + v.e_[2]};
}

inline auto operator-(const Vec3& u, const Vec3& v) -> Vec3 {
    return {u.e_[0] - v.e_[0], u.e_[1] - v.e_[1], u.e_[2] - v.e_[2]};
}

inline auto operator*(const Vec3& u, const Vec3& v) -> Vec3 {
    return {u.e_[0] * v.e_[0], u.e_[1] * v.e_[1], u.e_[2] * v.e_[2]};
}

inline auto operator*(Float t, const Vec3& v) -> Vec3 {
    return {t * v.e_[0], t * v.e_[1], t * v.e_[2]};
}

inline auto operator*(const Vec3& v, Float t) -> Vec3 { return t * v; }

inline auto operator/(const Vec3& v, Float t) -> Vec3 { return (1.0F / t) * v; }

inline auto Dot(const Vec3& u, const Vec3& v) -> float {
    return (u.e_[0] * v.e_[0]) + (u.e_[1] * v.e_[1]) + (u.e_[2] * v.e_[2]);
}

inline auto Cross(const Vec3& u, const Vec3& v) -> Vec3 {
    return {(u.e_[1] * v.e_[2]) - (u.e_[2] * v.e_[1]), (u.e_[2] * v.e_[0]) - (u.e_[0] * v.e_[2]),
            (u.e_[0] * v.e_[1]) - (u.e_[1] * v.e_[0])};
}

inline auto Normalize(const Vec3& v) -> Vec3 { return v / v.Length(); }

// A ray v coming in down-right with a normal n pointing straight up hits the surface,
// the downward force must reflect while the sideways motion remains constant.
// We isolate downward force by projecting v onto n (b)
// b = (v dot n) * n     <-   dot results in scalar so multiply by n for downward dir
// -b just negates downward motion, so -2b reflects it opposite way
inline auto Reflect(const Vec3& v, const Vec3& n) -> Vec3 { return v - 2 * Dot(v, n) * n; }

// Based on snells law: eta * sin(theta) = eta' * sin(theta')
// refrated ray = sin(theta') = eta/eta' * sin(theta)
// On refracted side of surface, there's refracted ray R' and normal n', with angle theta'
// R' can be split into perpendicular R' and parallel R' (to the normal n')
// messy final equation but treated as fact for now
inline auto Refract(const Vec3& uv, const Vec3& n, Float etai_over_etat) -> Vec3 {
    float cos_theta = std::fmin(Dot(-uv, n), 1.0F);
    Vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    Vec3 r_out_parallel = -std::sqrt(std::fabs(1.0F - r_out_perp.LengthSquared())) * n;
    return r_out_perp + r_out_parallel;
}

}  // namespace skwr

#endif  // SKWR_CORE_VEC3_H_
