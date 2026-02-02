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
