#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "core/math/quat.h"
#include "core/math/transform.h"
#include "scene/animation.h"
#include "scene/interp_curve.h"

namespace skwr {

namespace {

std::shared_ptr<const InterpolationCurve> SharedLinear() {
    static BezierCurve k(0, 0, 1, 1);
    return std::shared_ptr<const InterpolationCurve>(&k, [](const InterpolationCurve*) {});
}

Keyframe K(float time, const TRS& trs,
           const std::shared_ptr<const InterpolationCurve>& curve =
               std::shared_ptr<const InterpolationCurve>()) {
    Keyframe kf;
    kf.time = time;
    kf.transform = trs;
    kf.curve = curve ? curve : SharedLinear();
    return kf;
}

bool QuatNear(const Quat& a, const Quat& b, float eps = 1e-4f) {
    float d = std::fabs(QuatDot(a, b));
    return d > 1.0f - eps;
}

}  // namespace

TEST(Animation, StaticSingleKeyframe) {
    AnimatedTransform anim;
    TRS only =
        TRSFromEuler(Vec3(1.0f, 2.0f, 3.0f), Vec3(10.0f, 20.0f, 30.0f), Vec3(2.0f, 2.0f, 2.0f));
    anim.keyframes.push_back(K(0.0f, only));
    for (float t : {-100.0f, 0.0f, 0.5f, 100.0f}) {
        TRS e = anim.Evaluate(t);
        EXPECT_NEAR(e.translation.x(), only.translation.x(), 1e-5f);
        EXPECT_NEAR(e.scale.x(), only.scale.x(), 1e-5f);
        EXPECT_TRUE(QuatNear(e.rotation, only.rotation));
    }
}

TEST(Animation, TwoKeyframesLinearMidpointTranslationAndRotation) {
    TRS a = TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS b = TRSFromEuler(Vec3(2.0f, 0.0f, 0.0f), Vec3(0.0f, 90.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    AnimatedTransform anim;
    anim.keyframes.push_back(K(0.0f, a));
    anim.keyframes.push_back(K(2.0f, b));

    TRS mid = anim.Evaluate(1.0f);
    EXPECT_NEAR(mid.translation.x(), 1.0f, 1e-5f);
    Quat expect_half = QuatNormalize(QuatSlerp(a.rotation, b.rotation, 0.5f));
    EXPECT_TRUE(QuatNear(mid.rotation, expect_half));
}

TEST(Animation, EasedSegmentUsesCurveNotHalfway) {
    TRS a = TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS b = TRSFromEuler(Vec3(0.0f, 0.0f, 10.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    AnimatedTransform linear_anim;
    Keyframe k0;
    k0.time = 0.0f;
    k0.transform = a;
    k0.curve = SharedLinear();
    Keyframe k1;
    k1.time = 1.0f;
    k1.transform = b;
    k1.curve = SharedLinear();
    linear_anim.keyframes = {k0, k1};

    AnimatedTransform ease_anim = linear_anim;
    static BezierCurve kEase = BezierCurve::EaseIn();
    ease_anim.keyframes[1].curve =
        std::shared_ptr<const InterpolationCurve>(&kEase, [](const InterpolationCurve*) {});

    float lin_z = linear_anim.Evaluate(0.5f).translation.z();
    float ease_z = ease_anim.Evaluate(0.5f).translation.z();
    EXPECT_NEAR(lin_z, 5.0f, 1e-5f);
    EXPECT_LT(ease_z, lin_z);
}

TEST(Animation, ClampBelowFirst) {
    AnimatedTransform anim;
    TRS a = TRSFromEuler(Vec3(3.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS b = TRSFromEuler(Vec3(5.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    anim.keyframes = {K(1.0f, a), K(3.0f, b)};
    TRS e = anim.Evaluate(-10.0f);
    EXPECT_NEAR(e.translation.x(), 3.0f, 1e-5f);
}

TEST(Animation, ClampAboveLast) {
    AnimatedTransform anim;
    TRS a = TRSFromEuler(Vec3(3.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS b = TRSFromEuler(Vec3(5.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    anim.keyframes = {K(1.0f, a), K(3.0f, b)};
    TRS e = anim.Evaluate(100.0f);
    EXPECT_NEAR(e.translation.x(), 5.0f, 1e-5f);
}

TEST(Animation, ThreeKeyframesMiddleSegment) {
    TRS t0 = TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS t1 = TRSFromEuler(Vec3(10.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS t2 = TRSFromEuler(Vec3(100.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    AnimatedTransform anim;
    anim.keyframes = {K(0.0f, t0), K(1.0f, t1), K(2.0f, t2)};
    TRS e = anim.Evaluate(0.5f);
    EXPECT_NEAR(e.translation.x(), 5.0f, 1e-5f);
    EXPECT_GT(e.translation.x(), 0.5f);
    EXPECT_LT(e.translation.x(), 50.0f);
}

TEST(Animation, SortKeyframes) {
    AnimatedTransform anim;
    anim.keyframes = {K(2.0f, TRSFromEuler(Vec3(2.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f),
                                           Vec3(1.0f, 1.0f, 1.0f))),
                      K(0.0f, TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f),
                                           Vec3(1.0f, 1.0f, 1.0f)))};
    anim.SortKeyframes();
    EXPECT_FLOAT_EQ(anim.keyframes[0].time, 0.0f);
    EXPECT_FLOAT_EQ(anim.keyframes[1].time, 2.0f);
}

TEST(Animation, IsStatic) {
    AnimatedTransform a;
    EXPECT_TRUE(a.keyframes.empty() || a.IsStatic());
    a.keyframes.push_back(K(0.0f, TRS{}));
    EXPECT_TRUE(a.IsStatic());
    a.keyframes.push_back(K(1.0f, TRS{}));
    EXPECT_FALSE(a.IsStatic());
}

}  // namespace skwr
