#ifndef SKWR_CORE_Ray_H_
#define SKWR_CORE_Ray_H_

#include "core/vec3.h"

namespace skwr {

class Ray {
  public:
    Ray() = default;

    // Ray(const Point3& origin, const Vec3& direction, double time)
    //     : orig(origin), dir(direction), tm(time) {}

    Ray(const Point3& origin, const Vec3& direction) : orig_(origin), dir_(direction) {
        // Precompute inverse optimization
        // IEEE 754 floating point handles 1.0/0.0 as Infinity, which works
        // correctly with the slab method intersection logic.
        inv_dir_ = Vec3(1.0F / direction.X(), 1.0F / direction.Y(), 1.0F / direction.Z());
    }

    [[nodiscard]] auto Origin() const -> const Point3& { return orig_; }
    [[nodiscard]] auto Direction() const -> const Vec3& { return dir_; }
    [[nodiscard]] auto InvDirection() const -> const Vec3& { return inv_dir_; }
    // double time() const { return tm_; }

    // 3D pos (P) on Ray is function of P(t) = A + tb, A = origin, b = Ray direction
    [[nodiscard]] auto At(float t) const -> Point3 { return orig_ + t * dir_; }

  private:
    Point3 orig_;
    Vec3 dir_;
    Vec3 inv_dir_;
    // double tm_;
};

}  // namespace skwr

#endif  // SKWR_CORE_Ray_H_
