#ifndef SKWR_CORE_RAY_H_
#define SKWR_CORE_RAY_H_

#include "core/math/vec3.h"
#include "core/sampling/volume_stack.h"

namespace skwr {

class Ray {
  public:
    Ray() {}

    // Ray(const Point3& origin, const Vec3& direction, double time)
    //     : orig(origin), dir(direction), tm(time) {}

    Ray(const Point3& origin, const Vec3& direction) : orig_(origin), dir_(direction) {
        // Precompute inverse optimization
        // IEEE 754 floating point handles 1.0/0.0 as Infinity, which works
        // correctly with the slab method intersection logic.
        inv_dir_ = Vec3(1.0f / direction.x(), 1.0f / direction.y(), 1.0f / direction.z());
    }

    const Point3& origin() const { return orig_; }
    const Vec3& direction() const { return dir_; }
    const Vec3& inv_direction() const { return inv_dir_; }
    const VolumeStack& vol_stack() const { return vol_stack_; }
    // double time() const { return tm_; }

    // 3D pos (P) on Ray is function of P(t) = A + tb, A = origin, b = Ray direction
    Point3 at(double t) const { return orig_ + t * dir_; }

  private:
    Point3 orig_;
    Vec3 dir_;
    Vec3 inv_dir_;
    VolumeStack vol_stack_;  // 17 bytes. TODO: move to PathState struct when DDGeom
    // double tm_;
};

}  // namespace skwr

#endif  // SKWR_CORE_RAY_H_
