#include <gtest/gtest.h>

#include "core/math/transform.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "geometry/animated_sphere.h"
#include "media/mediums.h"
#include "scene/animation.h"
#include "scene/camera.h"

namespace skwr {

TEST(MotionBlur, CameraShutterSamplesRayTimeInRange) {
    Vec3 from(0.0f, 0.0f, 0.0f);
    Vec3 at(0.0f, 0.0f, -1.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);
    Camera cam(from, at, up, 60.0f, 1.0f, 0.0f, 1.0f, 0.1f, 0.9f);
    RNG rng(0, 42);
    for (int i = 0; i < 32; ++i) {
        Ray r = cam.GetRay(0.5f, 0.5f, rng);
        EXPECT_GE(r.time(), 0.1f);
        EXPECT_LE(r.time(), 0.9f);
    }
}

TEST(MotionBlur, CameraZeroShutterAllRaysSameTime) {
    Vec3 from(0.0f, 0.0f, 0.0f);
    Vec3 at(0.0f, 0.0f, -1.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);
    Camera cam(from, at, up, 60.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    RNG rng(0, 7);
    for (int i = 0; i < 8; ++i) {
        Ray r = cam.GetRay(0.1f, 0.2f, rng);
        EXPECT_NEAR(r.time(), 0.0f, 1e-6f);
    }
}

TEST(CameraAnimation, TimelineCarriesForwardAndInterpolatesOptics) {
    CameraTimeline timeline;
    timeline.base.look_from = Vec3(0.0f, 0.0f, 0.0f);
    timeline.base.look_at = Vec3(0.0f, 0.0f, -1.0f);
    timeline.base.vup = Vec3(0.0f, 1.0f, 0.0f);
    timeline.base.vfov = 40.0f;
    timeline.base.aperture_radius = 0.1f;
    timeline.base.focus_distance = 2.0f;

    CameraKeyframe k0;
    k0.time = 0.0f;
    k0.state = timeline.base;
    CameraKeyframe k1;
    k1.time = 2.0f;
    k1.state = k0.state;
    k1.state.look_from = Vec3(4.0f, 0.0f, 0.0f);
    k1.state.aperture_radius = 0.5f;
    k1.state.focus_distance = 6.0f;
    timeline.keyframes = {k0, k1};

    CameraState mid = timeline.Evaluate(1.0f);
    EXPECT_NEAR(mid.look_from.x(), 2.0f, 1e-5f);
    EXPECT_NEAR(mid.look_at.z(), -1.0f, 1e-5f);
    EXPECT_NEAR(mid.aperture_radius, 0.3f, 1e-5f);
    EXPECT_NEAR(mid.focus_distance, 4.0f, 1e-5f);
}

TEST(CameraAnimation, RayOriginUsesPoseAtSampledTime) {
    CameraTimeline timeline;
    timeline.base.look_from = Vec3(0.0f, 0.0f, 0.0f);
    timeline.base.look_at = Vec3(0.0f, 0.0f, -1.0f);
    timeline.base.vup = Vec3(0.0f, 1.0f, 0.0f);
    timeline.base.vfov = 60.0f;
    timeline.base.aperture_radius = 0.0f;
    timeline.base.focus_distance = 1.0f;

    CameraKeyframe k0;
    k0.time = 0.0f;
    k0.state = timeline.base;
    CameraKeyframe k1;
    k1.time = 1.0f;
    k1.state = k0.state;
    k1.state.look_from = Vec3(10.0f, 0.0f, 0.0f);
    k1.state.look_at = Vec3(10.0f, 0.0f, -1.0f);
    timeline.keyframes = {k0, k1};

    Camera cam(timeline, 1.0f, 0.0f, 1.0f);
    RNG rng(0, 123);
    Vec3 cam_forward;
    Ray r = cam.GetRay(0.5f, 0.5f, rng, &cam_forward);

    EXPECT_GT(r.origin().x(), 0.0f);
    EXPECT_LT(r.origin().x(), 10.0f);
    EXPECT_NEAR(cam_forward.z(), -1.0f, 1e-4f);
}

TEST(MotionBlur, TRSInverseApplyRoundTripPoint) {
    TRS trs =
        TRSFromEuler(Vec3(1.0f, 2.0f, 3.0f), Vec3(10.0f, 20.0f, 30.0f), Vec3(2.0f, 2.0f, 2.0f));
    Vec3 p(0.5f, -0.25f, 1.0f);
    Vec3 q = TRSApplyPoint(trs, p);
    Vec3 back = TRSInverseApplyPoint(trs, q);
    EXPECT_NEAR(back.x(), p.x(), 1e-4f);
    EXPECT_NEAR(back.y(), p.y(), 1e-4f);
    EXPECT_NEAR(back.z(), p.z(), 1e-4f);
}

TEST(MotionBlur, TRSInverseApplyVectorRoundTrip) {
    TRS trs = TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 45.0f, 0.0f), Vec3(3.0f, 3.0f, 3.0f));
    Vec3 v = Normalize(Vec3(1.0f, 1.0f, 0.0f));
    Vec3 w = TRSApplyVector(trs, v);
    Vec3 back = TRSInverseApplyVector(trs, w);
    EXPECT_NEAR(back.x(), v.x(), 1e-4f);
    EXPECT_NEAR(back.y(), v.y(), 1e-4f);
    EXPECT_NEAR(back.z(), v.z(), 1e-4f);
}

TEST(MotionBlur, AnimatedSphereEvaluatesAtTime) {
    SphereData sd{};
    sd.center = Vec3(0.0f, 0.0f, 0.0f);
    sd.radius = 1.0f;
    sd.material_id = kNullMaterialId;
    sd.interior_medium = kVacuumMediumId;
    sd.exterior_medium = kVacuumMediumId;

    AnimatedTransform anim;
    Keyframe k0;
    k0.time = 0.0f;
    k0.transform.translation = Vec3(0.0f, 0.0f, 0.0f);
    Keyframe k1;
    k1.time = 1.0f;
    k1.transform.translation = Vec3(4.0f, 0.0f, 0.0f);
    anim.keyframes = {k0, k1};

    AnimatedSphere as;
    as.local_data = sd;
    as.transform_chain = {anim};

    Sphere s_mid = as.EvaluateAt(0.5f);
    EXPECT_NEAR(s_mid.center.x(), 2.0f, 1e-4f);
}

}  // namespace skwr
