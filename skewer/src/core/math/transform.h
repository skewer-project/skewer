#ifndef SKWR_CORE_MATH_TRANSFORM_H_
#define SKWR_CORE_MATH_TRANSFORM_H_

#include <cmath>

#include "core/math/constants.h"
#include "core/math/quat.h"
#include "core/math/utils.h"
#include "core/math/vec3.h"
#include "geometry/boundbox.h"

namespace skwr {

struct TRS {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

inline TRS TRSFromEuler(const Vec3& translate, const Vec3& rotate_deg, const Vec3& scale) {
    float rx = DegreesToRadians(rotate_deg.x());
    float ry = DegreesToRadians(rotate_deg.y());
    float rz = DegreesToRadians(rotate_deg.z());
    return TRS{translate, QuatNormalize(QuatFromEulerYXZ(rx, ry, rz)), scale};
}

inline TRS Compose(const TRS& parent, const TRS& child) {
    TRS out{};
    out.scale = parent.scale * child.scale;
    out.rotation = QuatNormalize(QuatMultiply(parent.rotation, child.rotation));
    Vec3 st =
        Vec3(child.translation.x() * parent.scale.x(), child.translation.y() * parent.scale.y(),
             child.translation.z() * parent.scale.z());
    out.translation = parent.translation + QuatRotate(parent.rotation, st);
    return out;
}

inline Vec3 TRSApplyPoint(const TRS& trs, const Vec3& p) {
    Vec3 scaled = trs.scale * p;
    return trs.translation + QuatRotate(trs.rotation, scaled);
}

inline Vec3 TRSInverseApplyVector(const TRS& trs, const Vec3& v) {
    Vec3 r = QuatRotate(QuatConjugate(trs.rotation), v);
    float sx = trs.scale.x(), sy = trs.scale.y(), sz = trs.scale.z();
    if (std::fabs(sx) < Numeric::kNearZeroEpsilon || std::fabs(sy) < Numeric::kNearZeroEpsilon ||
        std::fabs(sz) < Numeric::kNearZeroEpsilon) {
        return r;
    }
    return Vec3(r.x() / sx, r.y() / sy, r.z() / sz);
}

inline Vec3 TRSInverseApplyPoint(const TRS& trs, const Vec3& p) {
    Vec3 q = p - trs.translation;
    return TRSInverseApplyVector(trs, q);
}

inline Vec3 TRSApplyVector(const TRS& trs, const Vec3& v) {
    return QuatRotate(trs.rotation, trs.scale * v);
}

inline Vec3 TRSApplyNormal(const TRS& trs, const Vec3& n) {
    float sx = trs.scale.x(), sy = trs.scale.y(), sz = trs.scale.z();
    if (std::fabs(sx) < Numeric::kNearZeroEpsilon || std::fabs(sy) < Numeric::kNearZeroEpsilon ||
        std::fabs(sz) < Numeric::kNearZeroEpsilon) {
        return Normalize(QuatRotate(trs.rotation, n));
    }
    Vec3 inv_scaled(n.x() / sx, n.y() / sy, n.z() / sz);
    return Normalize(QuatRotate(trs.rotation, inv_scaled));
}

inline bool TRSIsUniformScale(const TRS& trs, float eps = 1e-5f) {
    float sx = trs.scale.x(), sy = trs.scale.y(), sz = trs.scale.z();
    return std::fabs(sx - sy) <= eps && std::fabs(sy - sz) <= eps;
}

inline BoundBox TransformBounds(const TRS& trs, const BoundBox& local) {
    BoundBox world;
    const Point3& mn = local.min();
    const Point3& mx = local.max();
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? mx.x() : mn.x();
        float y = (i & 2) ? mx.y() : mn.y();
        float z = (i & 4) ? mx.z() : mn.z();
        world.Expand(TRSApplyPoint(trs, Point3(x, y, z)));
    }
    world.PadToMinimums();
    return world;
}

inline bool TRSIsIdentity(const TRS& trs) {
    if (std::fabs(trs.translation.x()) > Numeric::kNearZeroEpsilon ||
        std::fabs(trs.translation.y()) > Numeric::kNearZeroEpsilon ||
        std::fabs(trs.translation.z()) > Numeric::kNearZeroEpsilon) {
        return false;
    }
    if (std::fabs(trs.scale.x() - 1.0f) > Numeric::kNearZeroEpsilon ||
        std::fabs(trs.scale.y() - 1.0f) > Numeric::kNearZeroEpsilon ||
        std::fabs(trs.scale.z() - 1.0f) > Numeric::kNearZeroEpsilon) {
        return false;
    }
    Quat rq = QuatNormalize(trs.rotation);
    float imag_len = std::sqrt(rq.x * rq.x + rq.y * rq.y + rq.z * rq.z);
    float angle = 2.0f * std::atan2(imag_len, std::fabs(rq.w));
    return angle <= 1e-5f;
}

}  // namespace skwr

#endif  // SKWR_CORE_MATH_TRANSFORM_H_
