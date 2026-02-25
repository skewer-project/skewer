#ifndef SKWR_SCENE_CAMERA_H_
#define SKWR_SCENE_CAMERA_H_

#include <cmath>

#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/ray.h"

namespace skwr {

// LookAt camera - RTIOW stuff. later need to implement camera-to-world (but need mat4 for that)
// Basically RTIOW's camera, we just shifted the starting pixel to lower-left corner
class Camera {
  public:
    Camera(const Vec3& look_from, const Vec3& look_at, const Vec3& vup, float vfov,
           float aspect_ratio) {
        auto theta = vfov * kPi / 180.0f;
        auto h = std::tan(theta / 2.0f);
        auto viewport_height = 2.0f * h;
        auto viewport_width = aspect_ratio * viewport_height;

        // Calculate basis vecs
        w_ = Normalize(look_from - look_at);  // inverse forward (points backwards)
        u_ = Normalize(Cross(vup, w_));       // right
        v_ = Cross(w_, u_);                   // up

        origin_ = look_from;
        horizontal_ = u_ * viewport_width;
        vertical_ = v_ * viewport_height;

        // Lower left corner of image
        lower_left_corner_ = origin_ - horizontal_ / 2.0f - vertical_ / 2.0f - w_;
    }

    // Ray generation!!!
    // takes normalized coords from 0.0 to 1.0 and returns a World Ray
    Ray GetRay(float s, float t) const {
        return Ray(origin_,
                   Normalize(lower_left_corner_ + (horizontal_ * s) + (vertical_ * t) - origin_));
    }

    Vec3 GetW() const { return w_; }

  private:
    Vec3 origin_;
    Vec3 lower_left_corner_;  // We are using a standard right-handed system +X: Right, +Y: Up, -Z:
                              // Forward (Into the screen)
    Vec3 horizontal_;
    Vec3 vertical_;
    Vec3 u_, v_, w_;
};

}  // namespace skwr

#endif  // SKWR_SCENE_CAMERA_H_
