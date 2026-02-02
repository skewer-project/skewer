#ifndef SKWR_GEOMETRY_INTERSECT_SPHERE_H_
#define SKWR_GEOMETRY_INTERSECT_SPHERE_H_

#include <cmath>

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
    si->p = r.origin() + root * r.direction();
    si->n = (si->p - s.center) / s.radius;
    return true;
}

}  // namespace skwr

#endif  // SKWR_GEOMETRY_INTERSECT_SPHERE_H_
