#include <gtest/gtest.h>

#include <cmath>

#include "core/math/constants.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"

namespace {

skwr::Vec3 RotateEulerYXZRef(const skwr::Vec3& p, float rx, float ry, float rz) {
    float cy = std::cos(ry), sy = std::sin(ry);
    skwr::Vec3 r1(cy * p.x() + sy * p.z(), p.y(), -sy * p.x() + cy * p.z());
    float cx = std::cos(rx), sx = std::sin(rx);
    skwr::Vec3 r2(r1.x(), cx * r1.y() - sx * r1.z(), sx * r1.y() + cx * r1.z());
    float cz = std::cos(rz), sz = std::sin(rz);
    return skwr::Vec3(cz * r2.x() - sz * r2.y(), sz * r2.x() + cz * r2.y(), r2.z());
}

}  // namespace

namespace skwr {

TEST(Quat, EulerMatchesLegacyYXZ) {
    const Vec3 p(0.3f, -1.2f, 2.7f);
    const float rx = 0.41f;
    const float ry = -0.73f;
    const float rz = 1.05f;

    Quat q = QuatNormalize(QuatFromEulerYXZ(rx, ry, rz));
    Vec3 a = QuatRotate(q, p);
    Vec3 b = RotateEulerYXZRef(p, rx, ry, rz);

    EXPECT_NEAR(a.x(), b.x(), 1e-5f);
    EXPECT_NEAR(a.y(), b.y(), 1e-5f);
    EXPECT_NEAR(a.z(), b.z(), 1e-5f);
}

TEST(Quat, SlerpEndpoints) {
    Quat a = QuatFromEulerYXZ(0.0f, 0.0f, 0.0f);
    Quat b = QuatFromEulerYXZ(0.0f, MathConstants::kPi / 2.0f, 0.0f);
    a = QuatNormalize(a);
    b = QuatNormalize(b);

    Quat q0 = QuatSlerp(a, b, 0.0f);
    Quat q1 = QuatSlerp(a, b, 1.0f);

    EXPECT_NEAR(std::fabs(QuatDot(q0, a)), 1.0f, 1e-4f);
    EXPECT_NEAR(std::fabs(QuatDot(q1, b)), 1.0f, 1e-4f);
}

TEST(Quat, SlerpMidpoint) {
    Quat a = QuatNormalize(QuatFromEulerYXZ(0.0f, 0.0f, 0.0f));
    Quat b = QuatNormalize(QuatFromEulerYXZ(0.0f, MathConstants::kPi / 2.0f, 0.0f));
    Quat mid = QuatSlerp(a, b, 0.5f);
    Quat expect = QuatNormalize(QuatFromEulerYXZ(0.0f, MathConstants::kPi / 4.0f, 0.0f));
    EXPECT_NEAR(std::fabs(QuatDot(mid, expect)), 1.0f, 1e-4f);
}

TEST(Quat, SlerpHemisphereFlip) {
    Quat a{1.0f, 0.0f, 0.0f, 0.0f};
    Quat b{-1.0f, 0.0f, 0.0f, 0.0f};
    Quat r = QuatSlerp(a, b, 0.5f);
    r = QuatNormalize(r);
    EXPECT_NEAR(std::fabs(r.w), 1.0f, 1e-4f);
    EXPECT_NEAR(r.x, 0.0f, 1e-4f);
    EXPECT_NEAR(r.y, 0.0f, 1e-4f);
    EXPECT_NEAR(r.z, 0.0f, 1e-4f);
}

TEST(Quat, Two90DegYEquals180DegY) {
    float hpi = MathConstants::kPi / 2.0f;
    Quat q90 = QuatNormalize(QuatFromEulerYXZ(0.0f, hpi, 0.0f));
    Quat q180 = QuatMultiply(q90, q90);
    q180 = QuatNormalize(q180);

    Vec3 v(1.0f, 0.0f, 0.0f);
    Vec3 out = QuatRotate(q180, v);
    EXPECT_NEAR(out.x(), -1.0f, 1e-5f);
    EXPECT_NEAR(out.y(), 0.0f, 1e-5f);
    EXPECT_NEAR(out.z(), 0.0f, 1e-5f);
}

}  // namespace skwr
