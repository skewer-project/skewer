#ifndef SKWR_CORE_MATH_QUAT_H_
#define SKWR_CORE_MATH_QUAT_H_

#include <cmath>

#include "core/math/vec3.h"

namespace skwr {

struct Quat {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Quat QuatNormalize(const Quat& q) {
    float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (len <= 0.0f) {
        return Quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    float inv = 1.0f / len;
    return Quat{q.w * inv, q.x * inv, q.y * inv, q.z * inv};
}

inline Quat QuatConjugate(const Quat& q) { return Quat{q.w, -q.x, -q.y, -q.z}; }

inline Quat QuatMultiply(const Quat& a, const Quat& b) {
    return Quat{
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

inline float QuatDot(const Quat& a, const Quat& b) {
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

// Intrinsic YXZ: first rotate around local Y, then local X, then local Z.
// Produces the same matrix as Three.js `rotation.order = "YXZ"`: R = Ry * Rx * Rz.
inline Quat QuatFromEulerYXZ(float rx_rad, float ry_rad, float rz_rad) {
    float hx = 0.5f * rx_rad;
    float hy = 0.5f * ry_rad;
    float hz = 0.5f * rz_rad;

    float cx = std::cos(hx), sx = std::sin(hx);
    float cy = std::cos(hy), sy = std::sin(hy);
    float cz = std::cos(hz), sz = std::sin(hz);

    Quat qx{cx, sx, 0.0f, 0.0f};
    Quat qy{cy, 0.0f, sy, 0.0f};
    Quat qz{cz, 0.0f, 0.0f, sz};

    return QuatMultiply(QuatMultiply(qy, qx), qz);
}

inline Vec3 QuatRotate(const Quat& q, const Vec3& v) {
    Vec3 qv(q.x, q.y, q.z);
    return v + 2.0f * Cross(qv, Cross(qv, v) + q.w * v);
}

inline Quat QuatSlerp(const Quat& a, const Quat& b, float t) {
    Quat bn = b;
    float d = QuatDot(a, bn);
    if (d < 0.0f) {
        bn = Quat{-bn.w, -bn.x, -bn.y, -bn.z};
        d = -d;
    }

    d = std::fmin(1.0f, std::fmax(0.0f, d));

    if (d > 0.9995f) {
        Quat r{a.w + t * (bn.w - a.w), a.x + t * (bn.x - a.x), a.y + t * (bn.y - a.y),
               a.z + t * (bn.z - a.z)};
        return QuatNormalize(r);
    }

    float theta_0 = std::acos(d);
    float sin_theta_0 = std::sin(theta_0);
    if (sin_theta_0 <= 1e-6f) {
        Quat r{a.w + t * (bn.w - a.w), a.x + t * (bn.x - a.x), a.y + t * (bn.y - a.y),
               a.z + t * (bn.z - a.z)};
        return QuatNormalize(r);
    }

    float theta = theta_0 * t;
    float sin_theta = std::sin(theta);
    float s0 = std::cos(theta) - d * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;
    return Quat{a.w * s0 + bn.w * s1, a.x * s0 + bn.x * s1, a.y * s0 + bn.y * s1,
                a.z * s0 + bn.z * s1};
}

}  // namespace skwr

#endif  // SKWR_CORE_MATH_QUAT_H_
