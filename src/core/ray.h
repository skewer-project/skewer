#ifndef SKWR_CORE_Ray_H_
#define SKWR_CORE_Ray_H_

#include "core/Vec3.h"

namespace skwr {

class Ray {
  public:
    Ray() {}

    // Ray(const Point3& origin, const Vec3& direction, double time)
    //     : orig(origin), dir(direction), tm(time) {}

    Ray(const Point3& origin, const Vec3& direction) : orig_(origin), dir_(direction) {}

    const Point3& origin() const { return orig_; }
    const Vec3& direction() const { return dir_; }
    // double time() const { return tm_; }

    // 3D pos (P) on Ray is function of P(t) = A + tb, A = origin, b = Ray direction
    Point3 at(double t) const { return orig_ + t * dir_; }

  private:
    Point3 orig_;
    Vec3 dir_;
    // double tm_;
};

}  // namespace skwr

#endif  // SKWR_CORE_Ray_H_
