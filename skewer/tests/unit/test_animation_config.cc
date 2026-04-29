#include <gtest/gtest.h>

#include <cmath>

#include "io/scene_loader.h"

namespace skwr {

TEST(AnimationConfig, NumFramesIntegerFps) {
    AnimationConfig a{};
    a.start = 0.0f;
    a.end = 2.0f;
    a.fps = 24.0f;
    a.shutter_angle = 180.0f;
    EXPECT_EQ(a.NumFrames(), 48);
}

TEST(AnimationConfig, NumFramesZeroDuration) {
    AnimationConfig a{};
    a.start = 1.0f;
    a.end = 1.0f;
    a.fps = 24.0f;
    EXPECT_EQ(a.NumFrames(), 0);
}

TEST(AnimationConfig, NumFramesFractionalFps) {
    AnimationConfig a{};
    a.start = 0.0f;
    a.end = 1.0f;
    a.fps = 23.976f;
    EXPECT_EQ(a.NumFrames(), 24);
}

TEST(AnimationConfig, FrameWindowShutter180At24fps) {
    AnimationConfig a{};
    a.start = 0.0f;
    a.end = 10.0f;
    a.fps = 24.0f;
    a.shutter_angle = 180.0f;
    auto [o, c] = a.FrameWindow(0);
    EXPECT_FLOAT_EQ(o, 0.0f);
    const float expected_dt = (180.0f / 360.0f) / 24.0f;
    EXPECT_FLOAT_EQ(c - o, expected_dt);
    EXPECT_FLOAT_EQ(c, o + expected_dt);
}

TEST(AnimationConfig, FrameWindowShutter90) {
    AnimationConfig a{};
    a.start = 0.0f;
    a.end = 10.0f;
    a.fps = 24.0f;
    a.shutter_angle = 90.0f;
    auto [o, c] = a.FrameWindow(3);
    const float t0 = 3.0f / 24.0f;
    EXPECT_FLOAT_EQ(o, t0);
    EXPECT_NEAR(c - o, (90.0f / 360.0f) / 24.0f, 1e-6f);
}

TEST(AnimationConfig, FrameWindowShutter360) {
    AnimationConfig a{};
    a.start = 0.0f;
    a.end = 10.0f;
    a.fps = 24.0f;
    a.shutter_angle = 360.0f;
    auto [o, c] = a.FrameWindow(1);
    EXPECT_FLOAT_EQ(c - o, 1.0f / 24.0f);
}

TEST(AnimationConfig, LastFrameWindowBeforeEnd) {
    AnimationConfig a{};
    a.start = 0.0f;
    a.end = 2.0f;
    a.fps = 24.0f;
    a.shutter_angle = 180.0f;
    const int n = a.NumFrames();
    ASSERT_EQ(n, 48);
    auto [o, c] = a.FrameWindow(n - 1);
    EXPECT_LT(c, a.end);
    EXPECT_GE(o, a.start);
}

}  // namespace skwr
