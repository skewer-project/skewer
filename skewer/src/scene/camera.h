#ifndef SKWR_SCENE_CAMERA_H_
#define SKWR_SCENE_CAMERA_H_

#include <cmath>

#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/sampling/sampling.h"

namespace skwr {

// LookAt camera with thin-lens depth of field.
// Set aperture_radius=0 (default) for a pinhole camera.
class Camera {
  class Camera {
    public:
      Camera(const Vec3& look_from, const Vec3& look_at, const Vec3& vup, float vfov,
             float aspect_ratio, float aperture_radius = 0.0f, float focus_distance = 1.0f,
             float shutter_open = 0.0f, float shutter_close = 0.0f) {
          auto theta = vfov * MathConstants::kPi / 180.0f;
          auto h = std::tan(theta / 2.0f);
          auto viewport_height = 2.0f * h;
          auto viewport_width = aspect_ratio * viewport_height;

          // Calculate basis vecs
          w_ = Normalize(look_from - look_at);  // inverse forward (points backwards)
          u_ = Normalize(Cross(vup, w_));       // right
          v_ = Cross(w_, u_);                   // up

          origin_ = look_from;
          lens_radius_ = aperture_radius;
          shutter_open_ = shutter_open;
          shutter_close_ = shutter_close;

          // Scale viewport by focus_distance so all rays through a pixel converge at the focus plane
          horizontal_ = u_ * (viewport_width * focus_distance);
          vertical_ = v_ * (viewport_height * focus_distance);

          // Lower left corner of image
          lower_left_corner_ = origin_ - horizontal_ / 2.0f - vertical_ / 2.0f - w_ * focus_distance;
      }

      // Ray generation: takes normalized coords [0,1] and returns a world-space ray.
      // When lens_radius_ > 0, applies thin-lens DoF by sampling the aperture disk.
      // Randomly samples a time between shutter_open and shutter_close.
      Ray GetRay(float s, float t, RNG& rng) const {
          Vec3 offset(0.0f, 0.0f, 0.0f);
          if (lens_radius_ > 0.0f) {
              Vec3 rd = RandomInUnitDisk(rng) * lens_radius_;
              offset = u_ * rd.x() + v_ * rd.y();
          }

          float time = shutter_open_;
          if (shutter_close_ > shutter_open_) {
              time = rng.UniformFloat() * (shutter_close_ - shutter_open_) + shutter_open_;
          }

          Vec3 focal_point = lower_left_corner_ + horizontal_ * s + vertical_ * t;
          return Ray(origin_ + offset, Normalize(focal_point - origin_ - offset), time);
      }
  ...
    private:
      Vec3 origin_;
      Vec3 lower_left_corner_;  // We are using a standard right-handed system +X: Right, +Y: Up, -Z:
                                // Forward (Into the screen)
      Vec3 horizontal_;
      Vec3 vertical_;
      Vec3 u_, v_, w_;
      float lens_radius_ = 0.0f;
      float shutter_open_ = 0.0f;
      float shutter_close_ = 0.0f;
  };

};

}  // namespace skwr

#endif  // SKWR_SCENE_CAMERA_H_
