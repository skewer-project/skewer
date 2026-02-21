#ifndef SKWR_GEOMETRY_BOUNDBOX_H_
#define SKWR_GEOMETRY_BOUNDBOX_H_

#include <algorithm>
#include <limits>

#include "core/constants.h"
#include "core/ray.h"
#include "core/vec3.h"

namespace skwr {

class BoundBox {
  public:
    // Default Constructor: Creates an "Empty/Invalid" box.
    // In strict math, an empty box has min=+Inf, max=-Inf.
    // This allows the first Union() operation to just snap to the target.
    BoundBox() {
        float min_val = std::numeric_limits<float>::max();
        float max_val = std::numeric_limits<float>::lowest();
        min_ = Point3(min_val, min_val, min_val);
        max_ = Point3(max_val, max_val, max_val);
    }

    BoundBox(const Point3& p) : min_(p), max_(p) {}

    BoundBox(const Point3& p1, const Point3& p2) {
        min_ =
            Point3(std::fmin(p1.x(), p2.x()), std::fmin(p1.y(), p2.y()), std::fmin(p1.z(), p2.z()));
        max_ =
            Point3(std::fmax(p1.x(), p2.x()), std::fmax(p1.y(), p2.y()), std::fmax(p1.z(), p2.z()));
    }

    const Point3& min() const { return min_; }
    const Point3& max() const { return max_; }

    bool IsValid() const {
        return min_.x() <= max_.x() && min_.y() <= max_.y() && min_.z() <= max_.z();
    }

    Point3 Centroid() const {
        return min_ * 0.5f + max_ * 0.5f;  // (min + max) / 2
    }

    Vec3 Diagonal() const { return max_ - min_; }

    // Returns 0 for X, 1 for Y, 2 for Z
    int LongestAxis() const {
        Vec3 d = Diagonal();
        if (d.x() > d.y() && d.x() > d.z()) return 0;
        return (d.y() > d.z()) ? 1 : 2;
    }

    // Surface Area (Used for SAH heuristic later)
    float SurfaceArea() const { return 2.0f * HalfArea(); }

    float HalfArea() const {
        Vec3 d = Diagonal();
        return (d.x() * d.y() + d.x() * d.z() + d.y() * d.z());
    }

    /**
     * Ray-Box Intersection (Optimized Slab Method)
     * use pre-computed inverse to avoid dividing here
     */
    bool Intersect(const Ray& r, float t_min, float t_max) const {
        const Vec3& inv_d = r.inv_direction();
        const Point3& orig = r.origin();

        // X Axis
        float t0 = (min_.x() - orig.x()) * inv_d.x();
        float t1 = (max_.x() - orig.x()) * inv_d.x();
        t_min = std::max(t_min, std::min(t0, t1));
        t_max = std::min(t_max, std::max(t0, t1));

        // Y Ayis
        t0 = (min_.y() - orig.y()) * inv_d.y();
        t1 = (max_.y() - orig.y()) * inv_d.y();
        t_min = std::max(t_min, std::min(t0, t1));
        t_max = std::min(t_max, std::max(t0, t1));

        // Z Axis
        t0 = (min_.z() - orig.z()) * inv_d.z();
        t1 = (max_.z() - orig.z()) * inv_d.z();
        t_min = std::max(t_min, std::min(t0, t1));
        t_max = std::min(t_max, std::max(t0, t1));

        return t_max > t_min;
    }

    // 0 thickness error fix
    void PadToMinimums() {
        // Adjust based on scene scale
        // good for scenes sized 1.0 - 1000.0
        float delta = kBoundEpsilon;

        Vec3 diag = max_ - min_;

        // Check X
        if (diag.x() < delta) {
            min_[0] -= delta * 0.5f;
            max_[0] += delta * 0.5f;
        }
        // Check Y
        if (diag.y() < delta) {
            min_[1] -= delta * 0.5f;
            max_[1] += delta * 0.5f;
        }
        // Check Z
        if (diag.z() < delta) {
            min_[2] -= delta * 0.5f;
            max_[2] += delta * 0.5f;
        }
    }

    // Box Operations
    void Expand(const Point3& p) {
        min_ = Point3(std::fmin(min_.x(), p.x()), std::fmin(min_.y(), p.y()),
                      std::fmin(min_.z(), p.z()));
        max_ = Point3(std::fmax(max_.x(), p.x()), std::fmax(max_.y(), p.y()),
                      std::fmax(max_.z(), p.z()));
    }

    void Expand(const BoundBox& bbox) {
        min_ = Point3(std::fmin(min_.x(), bbox.min_.x()), std::fmin(min_.y(), bbox.min_.y()),
                      std::fmin(min_.z(), bbox.min_.z()));
        max_ = Point3(std::fmax(max_.x(), bbox.max_.x()), std::fmax(max_.y(), bbox.max_.y()),
                      std::fmax(max_.z(), bbox.max_.z()));
    }

  private:
    Point3 min_;
    Point3 max_;
};

// Global helpers
inline BoundBox Union(const BoundBox& a, const BoundBox& b) {
    BoundBox res = a;
    res.Expand(b);
    return res;
}

inline BoundBox Union(const BoundBox& a, const BoundBox& b, const BoundBox& c) {
    return Union(a, Union(b, c));
}

inline BoundBox Union(const BoundBox& a, const BoundBox& b, const BoundBox& c, const BoundBox& d) {
    return Union(Union(a, b), Union(c, d));
}

// inline BoundBox Union(const BoundBox& bbox, const Point3& p) {
//     return BoundBox(min(bbox.min(), p), max(bbox.max(), p));
// }

}  // namespace skwr

#endif
