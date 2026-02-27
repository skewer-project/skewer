#ifndef SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_
#define SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_

#include <cmath>

#include "core/vec3.h"
#include "geometry/triangle.h"
#include "scene/surface_interaction.h"

namespace skwr {

inline bool IntersectTriangle(const Ray& r, const Triangle& tri, float t_min, float t_max,
                              SurfaceInteraction* si) {
    // Moller-Trumbore: all geometry read from pre-baked Triangle fields,
    // no index buffer or vertex buffer indirection.
    Vec3 ray_cross_e2 = Cross(r.direction(), tri.e2);
    float det = Dot(tri.e1, ray_cross_e2);

    if (det > -1e-8f && det < 1e-8f) return false;

    float inv_det = 1.0f / det;
    Vec3 s = r.origin() - tri.p0;
    float u = inv_det * Dot(s, ray_cross_e2);

    if (u < 0.0f || u > 1.0f) return false;

    Vec3 s_cross_e1 = Cross(s, tri.e1);
    float v = inv_det * Dot(r.direction(), s_cross_e1);

    if (v < 0.0f || u + v > 1.0f) return false;

    float t = inv_det * Dot(tri.e2, s_cross_e1);
    if (t < t_min || t > t_max) return false;

    si->t = t;
    si->point = r.at(t);
    si->material_id = tri.material_id;

    // Barycentric interpolation of pre-baked normals.
    // For flat meshes n0==n1==n2==geometric normal, so no branch needed.
    float w = 1.0f - u - v;
    si->n_geom = Normalize(w * tri.n0 + u * tri.n1 + v * tri.n2);
    si->SetFaceNormal(r, si->n_geom);

    // Barycentric interpolation of UV coordinates.
    si->uv = w * tri.uv0 + u * tri.uv1 + v * tri.uv2;

    // Tangent frame is only needed for normal-mapped materials.
    if (tri.needs_tangent_frame) {
        Vec3 duv1 = tri.uv1 - tri.uv0;
        Vec3 duv2 = tri.uv2 - tri.uv0;
        float uv_det = duv1.x() * duv2.y() - duv1.y() * duv2.x();

        if (uv_det > 1e-8f || uv_det < -1e-8f) {
            float inv_uv_det = 1.0f / uv_det;
            si->dpdu = (duv2.y() * tri.e1 - duv1.y() * tri.e2) * inv_uv_det;
            si->dpdv = (-duv2.x() * tri.e1 + duv1.x() * tri.e2) * inv_uv_det;
        } else {
            // Degenerate UVs: build an arbitrary tangent frame from the normal.
            Vec3 n = si->n_geom;
            Vec3 axis = (std::abs(n.y()) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
            si->dpdu = Normalize(Cross(axis, n));
            si->dpdv = Cross(n, si->dpdu);
        }
    } else {
        si->dpdu = Vec3(0.0f, 0.0f, 0.0f);
        si->dpdv = Vec3(0.0f, 0.0f, 0.0f);
    }

    return true;
}

}  // namespace skwr

#endif  // SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_
