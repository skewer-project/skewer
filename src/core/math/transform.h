#ifndef SKWR_CORE_MATH_TRANSFORM_H_
#define SKWR_CORE_MATH_TRANSFORM_H_

#include <cmath>
#include <vector>

#include "core/math/constants.h"
#include "core/math/vec3.h"

namespace skwr {

// Rotate a point by Euler angles (in radians), applied in Y-X-Z order.
inline Vec3 RotateEulerYXZ(const Vec3& p, float rx, float ry, float rz) {
    // Y rotation (yaw)
    float cy = std::cos(ry), sy = std::sin(ry);
    Vec3 r1(cy * p.x() + sy * p.z(), p.y(), -sy * p.x() + cy * p.z());

    // X rotation (pitch)
    float cx = std::cos(rx), sx = std::sin(rx);
    Vec3 r2(r1.x(), cx * r1.y() - sx * r1.z(), sx * r1.y() + cx * r1.z());

    // Z rotation (roll)
    float cz = std::cos(rz), sz = std::sin(rz);
    return Vec3(cz * r2.x() - sz * r2.y(), sz * r2.x() + cz * r2.y(), r2.z());
}

// Apply Scale -> Rotate -> Translate to a set of vertex positions.
// Rotation angles are in degrees. Scale is per-axis.
inline void ApplyTransform(std::vector<Vec3>& vertices, const Vec3& translate,
                           const Vec3& rotate_deg, const Vec3& scale) {
    float rx = DegreesToRadians(rotate_deg.x());
    float ry = DegreesToRadians(rotate_deg.y());
    float rz = DegreesToRadians(rotate_deg.z());

    bool has_rotation = (rx != 0.0f || ry != 0.0f || rz != 0.0f);

    for (auto& v : vertices) {
        // Scale
        v = Vec3(v.x() * scale.x(), v.y() * scale.y(), v.z() * scale.z());

        // Rotate
        if (has_rotation) {
            v = RotateEulerYXZ(v, rx, ry, rz);
        }

        // Translate
        v = v + translate;
    }
}

// Apply the same rotation to normal vectors (no scale/translate).
// Normals need to be re-normalized after rotation.
inline void ApplyRotationToNormals(std::vector<Vec3>& normals, const Vec3& rotate_deg) {
    float rx = DegreesToRadians(rotate_deg.x());
    float ry = DegreesToRadians(rotate_deg.y());
    float rz = DegreesToRadians(rotate_deg.z());

    if (rx == 0.0f && ry == 0.0f && rz == 0.0f) return;

    for (auto& n : normals) {
        n = Normalize(RotateEulerYXZ(n, rx, ry, rz));
    }
}

}  // namespace skwr

#endif  // SKWR_CORE_MATH_TRANSFORM_H_
