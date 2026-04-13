#include <gtest/gtest.h>

#include <cmath>

#include "core/math/constants.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"

namespace {

// Intrinsic YXZ reference: R = Ry * Rx * Rz (matches Three.js "YXZ").
skwr::Vec3 RotateIntrinsicYXZ(const skwr::Vec3& p, float rx, float ry, float rz) {
    float cx = std::cos(rx), sx = std::sin(rx);
    float cy = std::cos(ry), sy = std::sin(ry);
    float cz = std::cos(rz), sz = std::sin(rz);

    // R = Ry * Rx * Rz, applied to p
    // Rz first
    skwr::Vec3 r1(cz * p.x() - sz * p.y(), sz * p.x() + cz * p.y(), p.z());
    // then Rx
    skwr::Vec3 r2(r1.x(), cx * r1.y() - sx * r1.z(), sx * r1.y() + cx * r1.z());
    // then Ry
    return skwr::Vec3(cy * r2.x() + sy * r2.z(), r2.y(), -sy * r2.x() + cy * r2.z());
}

}  // namespace

namespace skwr {

TEST(Quat, EulerMatchesIntrinsicYXZ) {
    const Vec3 p(0.3f, -1.2f, 2.7f);
    const float rx = 0.41f;
    const float ry = -0.73f;
    const float rz = 1.05f;

    Quat q = QuatNormalize(QuatFromEulerYXZ(rx, ry, rz));
    Vec3 a = QuatRotate(q, p);
    Vec3 b = RotateIntrinsicYXZ(p, rx, ry, rz);

    EXPECT_NEAR(a.x(), b.x(), 1e-5f);
    EXPECT_NEAR(a.y(), b.y(), 1e-5f);
    EXPECT_NEAR(a.z(), b.z(), 1e-5f);
}

TEST(Quat, SingleAxisMatchesBothConventions) {
    const Vec3 p(1.0f, 2.0f, 3.0f);
    float ry = MathConstants::kPi / 3.0f;

    Quat q = QuatNormalize(QuatFromEulerYXZ(0.0f, ry, 0.0f));
    Vec3 a = QuatRotate(q, p);
    Vec3 b = RotateIntrinsicYXZ(p, 0.0f, ry, 0.0f);

    EXPECT_NEAR(a.x(), b.x(), 1e-5f);
    EXPECT_NEAR(a.y(), b.y(), 1e-5f);
    EXPECT_NEAR(a.z(), b.z(), 1e-5f);
}

TEST(Quat, SculptureGardenArmadillo) {
    float rx = 29.0f * MathConstants::kPi / 180.0f;
    float ry = 230.0f * MathConstants::kPi / 180.0f;
    float rz = 0.0f;

    Quat q = QuatNormalize(QuatFromEulerYXZ(rx, ry, rz));
    Vec3 up = QuatRotate(q, Vec3(0.0f, 1.0f, 0.0f));
    Vec3 ref = RotateIntrinsicYXZ(Vec3(0.0f, 1.0f, 0.0f), rx, ry, rz);

    EXPECT_NEAR(up.x(), ref.x(), 1e-5f);
    EXPECT_NEAR(up.y(), ref.y(), 1e-5f);
    EXPECT_NEAR(up.z(), ref.z(), 1e-5f);

    // Intrinsic YXZ: the 29° X pitch should tilt in the local frame after 230° Y yaw,
    // so up.x should be non-zero (unlike extrinsic where up.x == 0 after Y-then-worldX).
    EXPECT_TRUE(std::fabs(up.x()) > 0.1f);
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
