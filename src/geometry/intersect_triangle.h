#ifndef SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_
#define SKWR_GEOMETRY_INTERSECT_TRIANGLE_H_

#include <cstdint>

#include "core/constants.h"
#include "core/vec3.h"
#include "geometry/mesh.h"
#include "geometry/triangle.h"
#include "scene/surface_interaction.h"

namespace skwr {

inline bool IntersectTriangle(const Ray &r, const Triangle &tri, const Mesh &mesh, Float t_min,
                              Float t_max, SurfaceInteraction *si) {
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

// class triangle : public hittable {
//   public:
//     // Constructor with vertices, material, and optional per-vertex normals and UVs
//     triangle(const point3& v0, const point3& v1, const point3& v2, shared_ptr<material> mat)
//         : v0(v0), v1(v1), v2(v2), mat(mat), has_vertex_normals(false), has_uvs(false) {
//         // Compute geometric normal
//         vec3 e1 = v1 - v0;
//         vec3 e2 = v2 - v0;
//         geometric_normal = unit_vector(cross(e1, e2));

//         set_bounding_box();
//     }

//     // Constructor with vertices, material, and per-vertex normals
//     triangle(const point3& v0, const point3& v1, const point3& v2, shared_ptr<material> mat,
//              const vec3& n0, const vec3& n1, const vec3& n2)
//         : v0(v0),
//           v1(v1),
//           v2(v2),
//           mat(mat),
//           n0(unit_vector(n0)),
//           n1(unit_vector(n1)),
//           n2(unit_vector(n2)),
//           has_vertex_normals(true),
//           has_uvs(false) {
//         // Compute geometric normal for backface determination
//         vec3 e1 = v1 - v0;
//         vec3 e2 = v2 - v0;
//         geometric_normal = unit_vector(cross(e1, e2));

//         set_bounding_box();
//     }

//     // Full constructor with vertices, material, per-vertex normals, and UVs
//     triangle(const point3& v0, const point3& v1, const point3& v2, shared_ptr<material> mat,
//              const vec3& n0, const vec3& n1, const vec3& n2, const vec3& uv0, const vec3& uv1,
//              const vec3& uv2)
//         : v0(v0),
//           v1(v1),
//           v2(v2),
//           mat(mat),
//           n0(unit_vector(n0)),
//           n1(unit_vector(n1)),
//           n2(unit_vector(n2)),
//           uv0(uv0),
//           uv1(uv1),
//           uv2(uv2),
//           has_vertex_normals(true),
//           has_uvs(true) {
//         // Compute geometric normal for backface determination
//         vec3 e1 = v1 - v0;
//         vec3 e2 = v2 - v0;
//         geometric_normal = unit_vector(cross(e1, e2));

//         set_bounding_box();
//     }

//     bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
//         // Moller-Trumbore intersection algorithm
//         const double epsilon = 1e-8;

//         vec3 e1 = v1 - v0;
//         vec3 e2 = v2 - v0;

//         vec3 h = cross(r.direction(), e2);
//         double a = dot(e1, h);

//         // Ray is parallel to triangle
//         if (std::fabs(a) < epsilon) return false;

//         double f = 1.0 / a;
//         vec3 s = r.origin() - v0;
//         double u = f * dot(s, h);

//         // Intersection is outside triangle
//         if (u < 0.0 || u > 1.0) return false;

//         vec3 q = cross(s, e1);
//         double v = f * dot(r.direction(), q);

//         // Intersection is outside triangle
//         if (v < 0.0 || u + v > 1.0) return false;

//         // Compute t to find intersection point
//         double t = f * dot(e2, q);

//         // Check if t is within the valid ray interval
//         if (!ray_t.contains(t)) return false;

//         // We have a valid hit - fill in the hit record
//         rec.t = t;
//         rec.p = r.at(t);
//         rec.mat = mat;

//         // Compute barycentric coordinates: w = 1 - u - v, where (w, u, v) correspond to (v0, v1,
//         // v2)
//         double w = 1.0 - u - v;

//         // Interpolate normal if we have vertex normals, otherwise use geometric normal
//         vec3 outward_normal;
//         if (has_vertex_normals) {
//             outward_normal = unit_vector(w * n0 + u * n1 + v * n2);
//         } else {
//             outward_normal = geometric_normal;
//         }
//         rec.set_face_normal(r, outward_normal);

//         // Interpolate UV coordinates if available
//         if (has_uvs) {
//             rec.u = w * uv0.x() + u * uv1.x() + v * uv2.x();
//             rec.v = w * uv0.y() + u * uv1.y() + v * uv2.y();
//         } else {
//             // Use barycentric coordinates as default UVs
//             rec.u = u;
//             rec.v = v;
//         }

//         return true;
//     }

//     aabb bounding_box() const override { return bbox; }

//   private:
//     point3 v0, v1, v2;  // Triangle vertices
//     shared_ptr<material> mat;
//     vec3 n0, n1, n2;        // Per-vertex normals (for smooth shading)
//     vec3 uv0, uv1, uv2;     // Per-vertex texture coordinates
//     vec3 geometric_normal;  // Face normal (for flat shading / backface)
//     aabb bbox;
//     bool has_vertex_normals;
//     bool has_uvs;

//     void set_bounding_box() {
//         // Compute AABB from the three vertices with a small padding
//         auto min_x = std::fmin(std::fmin(v0.x(), v1.x()), v2.x());
//         auto min_y = std::fmin(std::fmin(v0.y(), v1.y()), v2.y());
//         auto min_z = std::fmin(std::fmin(v0.z(), v1.z()), v2.z());

//         auto max_x = std::fmax(std::fmax(v0.x(), v1.x()), v2.x());
//         auto max_y = std::fmax(std::fmax(v0.y(), v1.y()), v2.y());
//         auto max_z = std::fmax(std::fmax(v0.z(), v1.z()), v2.z());

//         point3 min_point(min_x, min_y, min_z);
//         point3 max_point(max_x, max_y, max_z);

//         bbox = aabb(min_point, max_point);

//         // Pad the bounding box to avoid numerical issues with axis-aligned triangles
//         const double delta = 0.0001;
//         if (bbox.x.size() < delta) bbox.x = bbox.x.expand(delta);
//         if (bbox.y.size() < delta) bbox.y = bbox.y.expand(delta);
//         if (bbox.z.size() < delta) bbox.z = bbox.z.expand(delta);
//     }
// };
