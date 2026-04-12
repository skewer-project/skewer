#ifndef SKWR_CORE_MATH_QUATERNION_H_
#define SKWR_CORE_MATH_QUATERNION_H_

#include <cmath>
#include <iostream>

#include "core/math/vec3.h"

namespace skwr {

struct Quaternion {
    float x, y, z, w;

    Quaternion() : x(0), y(0), z(0), w(1) {}
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    static Quaternion Identity() { return Quaternion(0, 0, 0, 1); }

    float LengthSquared() const { return x * x + y * y + z * z + w * w; }
    float Length() const { return std::sqrt(LengthSquared()); }

    Quaternion Normalized() const {
        float len = Length();
        if (len == 0) return Identity();
        return Quaternion(x / len, y / len, z / len, w / len);
    }
};

inline float Dot(const Quaternion& q1, const Quaternion& q2) {
    return q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
}

// Spherical Linear Interpolation
inline Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t) {
    float cosHalfTheta = Dot(q1, q2);

    Quaternion target = q2;
    if (cosHalfTheta < 0) {
        target = Quaternion(-q2.x, -q2.y, -q2.z, -q2.w);
        cosHalfTheta = -cosHalfTheta;
    }

    if (std::abs(cosHalfTheta) >= 1.0f) {
        return q1;
    }

    float halfTheta = std::acos(cosHalfTheta);
    float sinHalfTheta = std::sqrt(1.0f - cosHalfTheta * cosHalfTheta);

    if (std::abs(sinHalfTheta) < 0.001f) {
        return Quaternion(q1.x * 0.5f + target.x * 0.5f, q1.y * 0.5f + target.y * 0.5f,
                          q1.z * 0.5f + target.z * 0.5f, q1.w * 0.5f + target.w * 0.5f);
    }

    float ratioA = std::sin((1 - t) * halfTheta) / sinHalfTheta;
    float ratioB = std::sin(t * halfTheta) / sinHalfTheta;

    return Quaternion(q1.x * ratioA + target.x * ratioB, q1.y * ratioA + target.y * ratioB,
                      q1.z * ratioA + target.z * ratioB, q1.w * ratioA + target.w * ratioB);
}

}  // namespace skwr

#endif  // SKWR_CORE_MATH_QUATERNION_H_
