#ifndef SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_
#define SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_

#include <cstdint>

#include "core/constants.h"
#include "core/vec3.h"
#include "geometry/mesh.h"
#include "geometry/triangle.h"
#include "scene/surface_interaction.h"

namespace skwr {

inline bool IntersectTriangle(const Ray& r, const Triangle& tri, const Mesh& mesh, Float t_min,
                              Float t_max, SurfaceInteraction* si) {
    /**
     * Mesh stores vertices of all triangles
     *  And indices of which vertices make up a triangle
     * Triangle stores the starting index
     */
    uint32_t i0 = mesh.indices[tri.v_idx];
    uint32_t i1 = mesh.indices[tri.v_idx + 1];
    uint32_t i2 = mesh.indices[tri.v_idx + 2];

    const Vec3 p0 = mesh.p[i0];
    const Vec3 p1 = mesh.p[i1];
    const Vec3 p2 = mesh.p[i2];

    // Moller-Trumbore alg
    Vec3 e1 = p1 - p0;
    Vec3 e2 = p2 - p0;
    Vec3 ray_cross_e2 = Cross(r.direction(), e2);
    Float det = Dot(e1, ray_cross_e2);

    // Culling: If det is near zero, ray is parallel to triangle.
    // If we want Backface Culling (faster), check if det < Epsilon.
    // For a Path Tracer, we usually want to hit backfaces (glass, paper), so we just check for 0.
    if (det > -1e-8 && det < 1e-8) return false;

    Float inv_det = 1.0f / det;
    Vec3 s = r.origin() - p0;
    Float u = inv_det * Dot(s, ray_cross_e2);

    if (u < 0.0f || u > 1.0f) return false;

    Vec3 s_cross_e1 = Cross(s, e1);
    Float v = inv_det * Dot(r.direction(), s_cross_e1);

    if (v < 0.0f || u + v > 1.0f) return false;

    // Calculate t
    Float t = inv_det * Dot(e2, s_cross_e1);
    if (t < t_min || t > t_max) return false;

    si->t = t;
    si->p = r.at(t);
    si->material_id = mesh.material_id;

    // Interpolate Normal (barycentric interpolation)
    //  w = 1 - u - v
    //  Normal = w*n0 + u*n1 + v*n2
    if (!mesh.n.empty()) {
        Vec3 n0 = mesh.n[i0];
        Vec3 n1 = mesh.n[i1];
        Vec3 n2 = mesh.n[i2];
        Float w = 1.0f - u - v;
        si->n = Normalize(w * n0 + u * n1 + v * n2);
    } else {
        // Fallback: Geometric Normal (Flat shading)
        si->n = Normalize(Cross(e1, e2));
    }

    // Ensure normal points against ray
    si->SetFaceNormal(r, si->n);

    return true;
}

}  // namespace skwr

#endif  // SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_
