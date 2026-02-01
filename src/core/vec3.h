#ifndef SKWR_CORE_VEC3_H_
#define SKWR_CORE_VEC3_H_

#include <cmath>
#include <iostream>

#include "core/constants.h"
#include "core/random_utils.h"

namespace skwr {

struct Vec3 {
  public:
    Float e[3];

    Vec3() : e{0, 0, 0} {}
    Vec3(Float e0, Float e1, Float e2) : e{e0, e1, e2} {}

    Float x() const { return e[0]; }
    Float y() const { return e[1]; }
    Float z() const { return e[2]; }

    Vec3 operator-() const { return Vec3(-e[0], -e[1], -e[2]); }
    Float operator[](int i) const { return e[i]; }
    Float& operator[](int i) { return e[i]; }

    Vec3& operator+=(const Vec3& v) {
        e[0] += v.e[0];
        e[1] += v.e[1];
        e[2] += v.e[2];
        return *this;
    }

    Vec3& operator*=(Float t) {
        e[0] *= t;
        e[1] *= t;
        e[2] *= t;
        return *this;
    }

    Vec3& operator/=(Float t) { return *this *= 1 / t; }

    // Utility Member Functions
    Float Length() const { return std::sqrt(Length_squared()); }
    Float Length_squared() const { return e[0] * e[0] + e[1] * e[1] + e[2] * e[2]; }

    // Return true if the vector is close to zero in all dimensions
    bool Near_zero() const {
        auto s = 1e-8;
        return (std::fabs(e[0]) < s) && (std::fabs(e[1]) < s) && (std::fabs(e[2]) < s);
    }

    // Generating arbitrary random vectors
    static Vec3 random() { return Vec3(random_float(), random_float(), random_float()); }

    static Vec3 random(Float min, Float max) {
        return Vec3(random_float(min, max), random_float(min, max), random_float(min, max));
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

inline Vec3 operator*(Float t, const Vec3& v) { return Vec3(t * v.e[0], t * v.e[1], t * v.e[2]); }

inline Vec3 operator*(const Vec3& v, Float t) { return t * v; }

inline Vec3 operator/(const Vec3& v, Float t) { return (1 / t) * v; }

inline Float Dot(const Vec3& u, const Vec3& v) {
    return u.e[0] * v.e[0] + u.e[1] * v.e[1] + u.e[2] * v.e[2];
}

inline Vec3 Cross(const Vec3& u, const Vec3& v) {
    return Vec3(u.e[1] * v.e[2] - u.e[2] * v.e[1], u.e[2] * v.e[0] - u.e[0] * v.e[2],
                u.e[0] * v.e[1] - u.e[1] * v.e[0]);
}

inline Vec3 unit_vector(const Vec3& v) { return v / v.Length(); }

/** TODO: Move these to a sampling header when we implement advanced sampling */

// Rejection method for generating random vector on surface of a unit sphere (simple but
// inefficient)
inline Vec3 random_unit_vector() {
    while (true) {
        auto p = Vec3::random(-1, 1);
        auto lensq = p.Length_squared();
        // Add lower bound to avoid underflow error (small values -> 0 near center of sphere)
        if (1e-160 < lensq &&
            lensq <= 1) {  // normalize to produce unit vector if it's within unit sphere
            return p / sqrt(lensq);
        }
    }
}

// Check if unit vector is on the same hemisphere as normal (want it pointing away from surface)
inline Vec3 random_on_hemisphere(const Vec3& normal) {
    Vec3 generated_unit_vec = random_unit_vector();
    if (Dot(generated_unit_vec, normal) > 0.0)  // aligned with normal
        return generated_unit_vec;
    else
        return -generated_unit_vec;
}

// A ray v coming in down-right with a normal n pointing straight up hits the surface,
// the downward force must reflect while the sideways motion remains constant.
// We isolate downward force by projecting v onto n (b)
// b = (v dot n) * n     <-   dot results in scalar so multiply by n for downward dir
// -b just negates downward motion, so -2b reflects it opposite way
inline Vec3 reflect(const Vec3& v, const Vec3& n) { return v - 2 * Dot(v, n) * n; }

// Based on snells law: eta * sin(theta) = eta' * sin(theta')
// refrated ray = sin(theta') = eta/eta' * sin(theta)
// On refracted side of surface, there's refracted ray R' and normal n', with angle theta'
// R' can be split into perpendicular R' and parallel R' (to the normal n')
// messy final equation but treated as fact for now
inline Vec3 refract(const Vec3& uv, const Vec3& n, Float etai_over_etat) {
    auto cos_theta = std::fmin(Dot(-uv, n), 1.0);
    Vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    Vec3 r_out_parallel = -std::sqrt(std::fabs(1.0 - r_out_perp.Length_squared())) * n;
    return r_out_perp + r_out_parallel;
}

// Defocus disk
inline Vec3 random_in_unit_disk() {
    while (true) {
        auto p = Vec3(random_float(-1, 1), random_float(-1, 1), 0);
        if (p.Length_squared() < 1) return p;
    }
}

}  // namespace skwr

#endif  // SKWR_CORE_VEC3_H_
