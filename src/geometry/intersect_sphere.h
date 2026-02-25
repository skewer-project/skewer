#ifndef SKWR_GEOMETRY_INTERSECT_SPHERE_H_
#define SKWR_GEOMETRY_INTERSECT_SPHERE_H_

#include <cmath>

#include "core/constants.h"
#include "core/ray.h"
#include "core/vec3.h"
#include "geometry/sphere.h"
#include "scene/surface_interaction.h"

namespace skwr {

inline bool IntersectSphere(const Ray& r, const Sphere& s, float t_min, float t_max,
                            SurfaceInteraction* si) {
    Vec3 oc = r.origin() - s.center;
    float a = Dot(r.direction(), r.direction());
    float half_b = Dot(oc, r.direction());
    float c = Dot(oc, oc) - s.radius * s.radius;
    float discriminant = half_b * half_b - a * c;
    if (discriminant < 0) return false;

    float sqrtd = std::sqrt(discriminant);
    float root = (-half_b - sqrtd) / a;
    if (root < t_min || root > t_max) {
        root = (-half_b + sqrtd) / a;
        if (root < t_min || root > t_max) return false;
    }

    si->t = root;
    si->point = r.origin() + root * r.direction();

    // Outward normal (before face-flipping)
    Vec3 outward_normal = (si->point - s.center) / s.radius;
    si->SetFaceNormal(r, outward_normal);
    si->material_id = s.material_id;

    // Spherical UV coordinates
    float theta = std::acos(std::clamp(-outward_normal.y(), -1.0f, 1.0f));
    float phi = std::atan2(-outward_normal.z(), outward_normal.x()) + kPi;
    si->uv = Vec3(phi / (2.0f * kPi), theta / kPi, 0.0f);

    // dpdu: partial derivative of sphere surface w.r.t. u (phi direction)
    // dpdu = 2pi * r * (nz, 0, -nx)
    si->dpdu = Vec3(2.0f * kPi * s.radius * outward_normal.z(), 0.0f,
                    -2.0f * kPi * s.radius * outward_normal.x());

    // dpdv: partial derivative w.r.t. v (theta direction)
    // dpdv = pi * r * (-ny*nx/sin_theta, sin_theta, -ny*nz/sin_theta)
    float sin_theta = std::sqrt(std::max(0.0f, 1.0f - outward_normal.y() * outward_normal.y()));
    if (sin_theta > 1e-5f) {
        si->dpdv = Vec3(-kPi * s.radius * outward_normal.y() * outward_normal.x() / sin_theta,
                        kPi * s.radius * sin_theta,
                        -kPi * s.radius * outward_normal.y() * outward_normal.z() / sin_theta);
    } else {
        // At poles dpdu is degenerate; build an arbitrary frame
        Vec3 axis =
            (std::abs(outward_normal.y()) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f);
        si->dpdu = Normalize(Cross(axis, outward_normal)) * (2.0f * kPi * s.radius);
        si->dpdv = Normalize(Cross(outward_normal, si->dpdu)) * (kPi * s.radius);
    }

    return true;
}

}  // namespace skwr

#endif  // SKWR_GEOMETRY_INTERSECT_SPHERE_H_
