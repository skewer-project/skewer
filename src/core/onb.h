#ifndef SKWR_CORE_ONB_H_
#define SKWR_CORE_ONB_H_

#include "core/constants.h"
#include "core/vec3.h"

/**
 * Orthonormal Basis
 * - A local coordinate system to mathematically orient samples on the surface
 * - Let's us transform b/w World space and local space easily
 * - All vectors are unit vectors
 */

namespace skwr {

class ONB {
  public:
    ONB() = default;

    // Build basis from normal vector
    // Creates u, v st (u, v, w) are all perpendicular to each other
    void BuildFromW(const Vec3& n) {
        axis_[2] = Normalize(n);  // w is the normal

        // Arbitrary helper vector that isn't parallel to w
        Vec3 helper_vec =
            (std::abs(axis_[2].X()) > kParallelThreshold) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);

        axis_[1] = Normalize(Cross(axis_[2], helper_vec));  // v
        axis_[0] = Cross(axis_[1], axis_[2]);               // u
    }

    [[nodiscard]] auto U() const -> const Vec3& { return axis_[0]; }
    [[nodiscard]] auto V() const -> const Vec3& { return axis_[1]; }
    [[nodiscard]] auto W() const -> const Vec3& { return axis_[2]; }

    // Transform vector to match local space
    [[nodiscard]] auto Local(const Vec3& a) const -> Vec3 {
        return (U() * a.X()) + (V() * a.Y()) + (W() * a.Z());
    }

  private:
    std::array<Vec3, 3> axis_{};  // u, v, w
};

}  // namespace skwr

#endif  // SKWR_CORE_ONB_H_
