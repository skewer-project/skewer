#include <gtest/gtest.h>

#include <cmath>

#include "core/math/constants.h"
#include "core/math/transform.h"
#include "core/math/vec3.h"

namespace skwr {

TEST(TRS, IdentityQuaternionNegativeW) {
    TRS trs{};
    trs.rotation = Quat{-1.0f, 0.0f, 0.0f, 0.0f};
    EXPECT_TRUE(TRSIsIdentity(trs));
}

TEST(TRS, FullCircleEulerIsIdentityTRS) {
    TRS trs =
        TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(360.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(TRSIsIdentity(trs));
}

TEST(TRS, ApplyNormalDegenerateScaleUsesRotationOnly) {
    TRS trs{};
    trs.rotation = QuatNormalize(QuatFromEulerYXZ(0.0f, MathConstants::kPi / 4.0f, 0.0f));
    trs.scale = Vec3(0.0f, 1.0f, 1.0f);
    Vec3 n(0.0f, 1.0f, 0.0f);
    Vec3 out = TRSApplyNormal(trs, n);
    Vec3 expect = Normalize(QuatRotate(trs.rotation, n));
    EXPECT_TRUE(std::isfinite(out.x()) && std::isfinite(out.y()) && std::isfinite(out.z()));
    EXPECT_NEAR(out.x(), expect.x(), 1e-5f);
    EXPECT_NEAR(out.y(), expect.y(), 1e-5f);
    EXPECT_NEAR(out.z(), expect.z(), 1e-5f);
}

TEST(TRS, IdentityPointAndNormal) {
    TRS id{};
    Vec3 p(3.0f, -2.0f, 1.5f);
    Vec3 n(0.0f, 1.0f, 1.0f);
    Vec3 pn = Normalize(n);

    Vec3 pt = TRSApplyPoint(id, p);
    Vec3 nt = TRSApplyNormal(id, pn);

    EXPECT_NEAR(pt.x(), p.x(), 1e-6f);
    EXPECT_NEAR(pt.y(), p.y(), 1e-6f);
    EXPECT_NEAR(pt.z(), p.z(), 1e-6f);
    EXPECT_NEAR(nt.x(), pn.x(), 1e-6f);
    EXPECT_NEAR(nt.y(), pn.y(), 1e-6f);
    EXPECT_NEAR(nt.z(), pn.z(), 1e-6f);
}

TEST(TRS, NormalOrthogonalToTangentUnderNonUniformScale) {
    Vec3 n(0.0f, 1.0f, 0.0f);
    Vec3 t(1.0f, 0.0f, 0.0f);
    TRS trs =
        TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(25.0f, -40.0f, 15.0f), Vec3(2.0f, 0.5f, 3.0f));

    Vec3 n_w = TRSApplyNormal(trs, n);
    Vec3 t_lin = Vec3(t.x() * trs.scale.x(), t.y() * trs.scale.y(), t.z() * trs.scale.z());
    Vec3 t_w = Normalize(QuatRotate(trs.rotation, t_lin));

    EXPECT_NEAR(Dot(n_w, t_w), 0.0f, 1e-5f);
}

TEST(TRS, ComposeParentRotate90YChildTranslateX) {
    TRS parent =
        TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 90.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS child =
        TRSFromEuler(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS world = Compose(parent, child);

    Vec3 origin = TRSApplyPoint(world, Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(origin.x(), 0.0f, 1e-5f);
    EXPECT_NEAR(origin.y(), 0.0f, 1e-5f);
    EXPECT_NEAR(origin.z(), -1.0f, 1e-5f);
}

TEST(TRS, ComposeAddsParentTranslation) {
    TRS parent =
        TRSFromEuler(Vec3(5.0f, 0.0f, 0.0f), Vec3(0.0f, 90.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS child =
        TRSFromEuler(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS world = Compose(parent, child);

    Vec3 origin = TRSApplyPoint(world, Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(origin.x(), 5.0f, 1e-5f);
    EXPECT_NEAR(origin.y(), 0.0f, 1e-5f);
    EXPECT_NEAR(origin.z(), -1.0f, 1e-5f);
}

}  // namespace skwr
