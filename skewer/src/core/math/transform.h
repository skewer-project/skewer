#ifndef SKWR_CORE_MATH_TRANSFORM_H_
#define SKWR_CORE_MATH_TRANSFORM_H_

#include "core/math/constants.h"
#include "core/math/quat.h"
#include "core/math/utils.h"
#include "core/math/vec3.h"

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

inline Vec3 TRSApplyNormal(const TRS& trs, const Vec3& n) {
    Vec3 inv_scaled(n.x() / trs.scale.x(), n.y() / trs.scale.y(), n.z() / trs.scale.z());
    return Normalize(QuatRotate(trs.rotation, inv_scaled));
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
    return std::fabs(rq.w - 1.0f) <= Numeric::kNearZeroEpsilon &&
           std::fabs(rq.x) <= Numeric::kNearZeroEpsilon &&
           std::fabs(rq.y) <= Numeric::kNearZeroEpsilon &&
           std::fabs(rq.z) <= Numeric::kNearZeroEpsilon;
}

}  // namespace skwr

#endif  // SKWR_CORE_MATH_TRANSFORM_H_
