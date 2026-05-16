#ifndef SKWR_SCENE_CAMERA_H_
#define SKWR_SCENE_CAMERA_H_

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/sampling/sampling.h"
#include "scene/interp_curve.h"

namespace skwr {

struct CameraState {
    Vec3 look_from;
    Vec3 look_at;
    Vec3 vup = Vec3(0.0f, 1.0f, 0.0f);
    float vfov = 90.0f;
    float aperture_radius = 0.0f;
    float focus_distance = 1.0f;
};

struct CameraKeyframe {
    float time = 0.0f;
    CameraState state;
    std::shared_ptr<const InterpolationCurve> curve;
};

struct CameraTimeline {
    CameraState base;
    std::vector<CameraKeyframe> keyframes;

    bool IsAnimated() const { return keyframes.size() > 1; }
    CameraState Evaluate(float t) const {
        if (keyframes.empty()) {
            return base;
        }
        if (keyframes.size() == 1) {
            return keyframes[0].state;
        }

        const CameraKeyframe& first = keyframes.front();
        const CameraKeyframe& last = keyframes.back();
        if (t <= first.time) {
            return first.state;
        }
        if (t >= last.time) {
            return last.state;
        }

        auto it =
            std::upper_bound(keyframes.begin(), keyframes.end(), t,
                             [](float time, const CameraKeyframe& k) { return time < k.time; });
        size_t i = static_cast<size_t>(std::distance(keyframes.begin(), it)) - 1;
        const CameraKeyframe& k0 = keyframes[i];
        const CameraKeyframe& k1 = keyframes[i + 1];
        float dt = k1.time - k0.time;
        if (dt <= 1e-20f) {
            return k1.state;
        }

        float local_u = std::clamp((t - k0.time) / dt, 0.0f, 1.0f);
        float alpha =
            k1.curve ? k1.curve->Evaluate(local_u) : BezierCurve::Linear().Evaluate(local_u);

        CameraState out;
        out.look_from = k0.state.look_from + (k1.state.look_from - k0.state.look_from) * alpha;
        out.look_at = k0.state.look_at + (k1.state.look_at - k0.state.look_at) * alpha;
        out.vup = k0.state.vup + (k1.state.vup - k0.state.vup) * alpha;
        out.vfov = k0.state.vfov + (k1.state.vfov - k0.state.vfov) * alpha;
        out.aperture_radius = k0.state.aperture_radius +
                              (k1.state.aperture_radius - k0.state.aperture_radius) * alpha;
        out.focus_distance =
            k0.state.focus_distance + (k1.state.focus_distance - k0.state.focus_distance) * alpha;
        return out;
    }

    void SortKeyframes() {
        std::sort(keyframes.begin(), keyframes.end(),
                  [](const CameraKeyframe& a, const CameraKeyframe& b) { return a.time < b.time; });
    }
};

struct CameraFrame {
    Vec3 origin;
    Vec3 lower_left_corner;
    Vec3 horizontal;
    Vec3 vertical;
    Vec3 u;
    Vec3 v;
    Vec3 w;
    float lens_radius = 0.0f;
};

// LookAt camera with thin-lens depth of field.
// Set aperture_radius=0 (default) for a pinhole camera.
class Camera {
  public:
    Camera(const Vec3& look_from, const Vec3& look_at, const Vec3& vup, float vfov,
           float aspect_ratio, float aperture_radius = 0.0f, float focus_distance = 1.0f,
           float shutter_open = 0.0f, float shutter_close = 0.0f)
        : Camera(MakeStaticTimeline(look_from, look_at, vup, vfov, aperture_radius, focus_distance),
                 aspect_ratio, shutter_open, shutter_close) {}

    Camera(CameraTimeline timeline, float aspect_ratio, float shutter_open = 0.0f,
           float shutter_close = 0.0f)
        : timeline_(std::move(timeline)),
          aspect_ratio_(aspect_ratio),
          shutter_open_(shutter_open),
          shutter_close_(shutter_close),
          animated_(timeline_.IsAnimated()),
          static_frame_(BuildFrame(timeline_.Evaluate(shutter_open_), aspect_ratio_)) {}

    // Ray generation: takes normalized coords [0,1] and returns a world-space ray.
    // When lens_radius_ > 0, applies thin-lens DoF by sampling the aperture disk.
    Ray GetRay(float s, float t, RNG& rng, Vec3* cam_forward = nullptr) const {
        float ray_time = shutter_open_ + rng.UniformFloat() * (shutter_close_ - shutter_open_);
        const CameraFrame frame =
            animated_ ? BuildFrame(timeline_.Evaluate(ray_time), aspect_ratio_) : static_frame_;
        if (cam_forward != nullptr) {
            *cam_forward = -frame.w;
        }

        Vec3 offset(0.0f, 0.0f, 0.0f);
        if (frame.lens_radius > 0.0f) {
            Vec3 rd = RandomInUnitDisk(rng) * frame.lens_radius;
            offset = frame.u * rd.x() + frame.v * rd.y();
        }
        Vec3 focal_point = frame.lower_left_corner + frame.horizontal * s + frame.vertical * t;
        Vec3 dir = Normalize(focal_point - frame.origin - offset);
        return Ray(frame.origin + offset, dir, ray_time);
    }

    Vec3 GetW() const { return static_frame_.w; }
    const CameraTimeline& Timeline() const { return timeline_; }

  private:
    static CameraTimeline MakeStaticTimeline(const Vec3& look_from, const Vec3& look_at,
                                             const Vec3& vup, float vfov, float aperture_radius,
                                             float focus_distance) {
        CameraTimeline timeline;
        timeline.base.look_from = look_from;
        timeline.base.look_at = look_at;
        timeline.base.vup = vup;
        timeline.base.vfov = vfov;
        timeline.base.aperture_radius = aperture_radius;
        timeline.base.focus_distance = focus_distance;
        return timeline;
    }

    static CameraFrame BuildFrame(const CameraState& state, float aspect_ratio) {
        auto theta = state.vfov * MathConstants::kPi / 180.0f;
        auto h = std::tan(theta / 2.0f);
        auto viewport_height = 2.0f * h;
        auto viewport_width = aspect_ratio * viewport_height;

        CameraFrame frame;
        frame.w = Normalize(state.look_from - state.look_at);  // inverse forward
        frame.u = Normalize(Cross(state.vup, frame.w));
        frame.v = Cross(frame.w, frame.u);
        frame.origin = state.look_from;
        frame.lens_radius = state.aperture_radius;
        frame.horizontal = frame.u * (viewport_width * state.focus_distance);
        frame.vertical = frame.v * (viewport_height * state.focus_distance);
        frame.lower_left_corner = frame.origin - frame.horizontal / 2.0f - frame.vertical / 2.0f -
                                  frame.w * state.focus_distance;
        return frame;
    }

    CameraTimeline timeline_;
    float aspect_ratio_ = 1.0f;
    float shutter_open_ = 0.0f;
    float shutter_close_ = 0.0f;
    bool animated_ = false;
    CameraFrame static_frame_;
};

}  // namespace skwr

#endif  // SKWR_SCENE_CAMERA_H_
