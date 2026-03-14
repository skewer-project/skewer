#ifndef SKWR_CORE_MATH_VEC3_H_
#define SKWR_CORE_MATH_VEC3_H_

#include <cmath>
#include <iostream>

namespace skwr {

struct Vec3 {
  public:
    float e[3];

    Vec3() : e{0, 0, 0} {}
    Vec3(float e0, float e1, float e2) : e{e0, e1, e2} {}

    float x() const { return e[0]; }
    float y() const { return e[1]; }
    float z() const { return e[2]; }

    Vec3 operator-() const { return Vec3(-e[0], -e[1], -e[2]); }
    float operator[](int i) const { return e[i]; }
    float& operator[](int i) { return e[i]; }

    Vec3& operator+=(const Vec3& v) {
        e[0] += v.e[0];
        e[1] += v.e[1];
        e[2] += v.e[2];
        return *this;
    }

    Vec3& operator*=(float t) {
        e[0] *= t;
        e[1] *= t;
        e[2] *= t;
        return *this;
    }

    Vec3& operator/=(float t) { return *this *= 1 / t; }

    // Utility Member Functions
    float Length() const { return std::sqrt(LengthSquared()); }
    float LengthSquared() const { return e[0] * e[0] + e[1] * e[1] + e[2] * e[2]; }

    // Return true if the vector is close to zero in all dimensions
    bool Near_zero() const {
        auto s = 1e-8;
        return (std::fabs(e[0]) < s) && (std::fabs(e[1]) < s) && (std::fabs(e[2]) < s);
    }
};

// point alias for Vec3
using Point3 = Vec3;

// Vector Utility Functions
inline std::ostream& operator<<(std::ostream& out, const Vec3& v) {
    return out << v.e[0] << ' ' << v.e[1] << ' ' << v.e[2];
}

inline Vec3 operator+(const Vec3& u, const Vec3& v) {
    return Vec3(u.e[0] + v.e[0], u.e[1] + v.e[1], u.e[2] + v.e[2]);
}

inline Vec3 operator-(const Vec3& u, const Vec3& v) {
    return Vec3(u.e[0] - v.e[0], u.e[1] - v.e[1], u.e[2] - v.e[2]);
}

inline Vec3 operator*(const Vec3& u, const Vec3& v) {
    return Vec3(u.e[0] * v.e[0], u.e[1] * v.e[1], u.e[2] * v.e[2]);
}

inline Vec3 operator*(float t, const Vec3& v) { return Vec3(t * v.e[0], t * v.e[1], t * v.e[2]); }

inline Vec3 operator*(const Vec3& v, float t) { return t * v; }

inline Vec3 operator/(const Vec3& v, float t) { return (1 / t) * v; }

inline float Dot(const Vec3& u, const Vec3& v) {
    return u.e[0] * v.e[0] + u.e[1] * v.e[1] + u.e[2] * v.e[2];
}

inline Vec3 Cross(const Vec3& u, const Vec3& v) {
    return Vec3(u.e[1] * v.e[2] - u.e[2] * v.e[1], u.e[2] * v.e[0] - u.e[0] * v.e[2],
                u.e[0] * v.e[1] - u.e[1] * v.e[0]);
}

inline Vec3 Normalize(const Vec3& v) { return v / v.Length(); }

// A ray v coming in down-right with a normal n pointing straight up hits the surface,
// the downward force must reflect while the sideways motion remains constant.
// We isolate downward force by projecting v onto n (b)
// b = (v dot n) * n     <-   dot results in scalar so multiply by n for downward dir
// -b just negates downward motion, so -2b reflects it opposite way
inline Vec3 Reflect(const Vec3& v, const Vec3& n) { return v - 2 * Dot(v, n) * n; }

// Based on snells law: eta * sin(theta) = eta' * sin(theta')
// refrated ray = sin(theta') = eta/eta' * sin(theta)
// On refracted side of surface, there's refracted ray R' and normal n', with angle theta'
// R' can be split into perpendicular R' and parallel R' (to the normal n')
// messy final equation but treated as fact for now
inline Vec3 Refract(const Vec3& uv, const Vec3& n, float etai_over_etat) {
    auto cos_theta = std::fmin(Dot(-uv, n), 1.0);
    Vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    Vec3 r_out_parallel = -std::sqrt(std::fabs(1.0 - r_out_perp.LengthSquared())) * n;
    return r_out_perp + r_out_parallel;
}

}  // namespace skwr

#endif  // SKWR_CORE_MATH_VEC3_H_
