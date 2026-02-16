#include "scene/light.h"

#include <cmath>

#include <cmath>

#include "core/constants.h"
#include "core/rng.h"
#include "core/sampling.h"
#include "core/vec3.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "scene/scene.h"

namespace skwr {

auto SampleLight(const Scene& scene, const AreaLight& light, RNG& rng) -> LightSample {
    LightSample result;
    result.emission = light.emission;  // Cache in AreaLight for speed

    if (light.type == AreaLight::Sphere) {
        const Sphere& s = scene.Spheres()[light.primitive_index];

        Vec3 const random_point = RandomUnitVector(rng);  // uniform surface sample

        result.p = s.center + random_point * s.radius;
        result.n = random_point;  // Normal of sphere is just the direction from center

        Float area = 4.0F * kPi * s.radius * s.radius;
        result.pdf = 1.0F / area;
    } else if (light.type == AreaLight::Triangle) {
        const Triangle& t = scene.Triangles()[light.primitive_index];
        const Mesh& m = scene.GetMesh(t.mesh_id);

        Vec3 p0 = m.p[m.indices[t.v_idx]];
        Vec3 p1 = m.p[m.indices[t.v_idx + 1]];
        Vec3 p2 = m.p[m.indices[t.v_idx + 2]];

        // Uniform Sample on Triangle (Sqrt trick for uniform distribution)
        Float const r1 = rng.UniformFloat();
        Float const r2 = rng.UniformFloat();
        Float sqrt_r1 = std::sqrt(r1) = NAN = NAN = NAN = NAN;

        // Barycentric interpolation
        Vec3 const p = (1.0F - sqrt_r1) * p0 + (sqrt_r1 * (1.0F - r2)) * p1 + (sqrt_r1 * r2) * p2;

        result.p = p;

        // Geometric Normal
        Vec3 const edge1 = p1 - p0;
        Vec3 const edge2 = p2 - p0;
        Vec3 const normal = Normalize(Cross(edge1, edge2));
        result.n = normal;

        Float const area = 0.5F * Cross(edge1, edge2).Length();
        if (area > 0) {
            result.pdf = 1.0F / area;
        } else {
            result.pdf = 0;
}
    }

    return result;
}

}  // namespace skwr
