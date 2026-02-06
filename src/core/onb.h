#ifndef SKWR_CORE_ONB_H_
#define SKWR_CORE_ONB_H_

#include "core/vec3.h"

/**
 * Orthonormal Basis
 * - A local coordinate system to mathematically orient samples on the surface
 * - Let's us transform b/w World space and local space easily
 * - All vectors are unit vectors
 */

namespace skwr {

struct ONB {
    ONB() {}

    // Build basis from normal vector
    // Creates u, v st (u, v, w) are all perpendicular to each other
    void BuildFromW(const Vec3 &n) {
        axis[2] = Normalize(n);  // w is the normal

        // Arbitrary helper vector that isn't parallel to w
        Vec3 a = (std::abs(axis[2].x()) > 0.9f) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);

        axis[1] = Normalize(Cross(axis[2], a));  // v
        axis[0] = Cross(axis[1], axis[2]);       // u
    }

    const Vec3 &u() const { return axis[0]; }
    const Vec3 &v() const { return axis[1]; }
    const Vec3 &w() const { return axis[2]; }

    // Transform vector to match local space
    Vec3 Local(Vec3 &a) const { return (u() * a.x()) + (v() * a.y()) + (w() * a.z()); }

    Vec3 axis[3];  // u, v, w
};

}  // namespace skwr

#endif  // SKWR_CORE_ONB_H_
