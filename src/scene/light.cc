#include "scene/light.h"

#include "core/sampling.h"
#include "geometry/sphere.h"
#include "scene/scene.h"

namespace skwr {

LightSample SampleLight(const Scene& scene, const AreaLight& light, RNG& rng) {
    LightSample result;
    result.emission = light.emission;  // Cache in AreaLight for speed

    if (light.type == AreaLight::Sphere) {
        const Sphere& s = scene.Spheres()[light.primitive_index];

        Vec3 random_point = RandomUnitVector(rng);  // uniform surface sample

        result.p = s.center + random_point * s.radius;
        result.n = random_point;  // Normal of sphere is just the direction from center

        Float area = 4.0f * kPi * s.radius * s.radius;
        result.pdf = 1.0f / area;
    } else if (light.type == AreaLight::Triangle) {
        const Triangle& t = scene.Triangles()[light.primitive_index];
        const Mesh& m = scene.GetMesh(t.mesh_id);

        Vec3 p0 = m.p[m.indices[t.v_idx]];
        Vec3 p1 = m.p[m.indices[t.v_idx + 1]];
        Vec3 p2 = m.p[m.indices[t.v_idx + 2]];

        // Uniform Sample on Triangle (Sqrt trick for uniform distribution)
        Float r1 = rng.UniformFloat();
        Float r2 = rng.UniformFloat();
        Float sqrt_r1 = std::sqrt(r1);

        // Barycentric interpolation
        Vec3 p = (1.0f - sqrt_r1) * p0 + (sqrt_r1 * (1.0f - r2)) * p1 + (sqrt_r1 * r2) * p2;

        result.p = p;

        // Geometric Normal
        Vec3 edge1 = p1 - p0;
        Vec3 edge2 = p2 - p0;
        Vec3 normal = Normalize(Cross(edge1, edge2));
        result.n = normal;

        Float area = 0.5f * Cross(edge1, edge2).Length();
        if (area > 0)
            result.pdf = 1.0f / area;
        else
            result.pdf = 0;
    }

    return result;
}

}  // namespace skwr
