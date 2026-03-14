#include "scene/light.h"

#include "core/sampling/sampling.h"
#include "geometry/sphere.h"
#include "scene/scene.h"

namespace skwr {

LightSample SampleLight(const Scene& scene, const AreaLight& light, RNG& rng) {
    LightSample result;
    result.emission = light.emission;

    if (light.type == AreaLight::Sphere) {
        const Sphere& s = scene.Spheres()[light.primitive_index];

        Vec3 random_point = RandomUnitVector(rng);
        result.p = s.center + random_point * s.radius;
        result.n = random_point;

        float area = 4.0f * kPi * s.radius * s.radius;
        result.pdf = 1.0f / area;
    } else if (light.type == AreaLight::Triangle) {
        const Triangle& t = scene.Triangles()[light.primitive_index];

        // Uniform sample on triangle (sqrt trick for uniform distribution)
        float r1 = rng.UniformFloat();
        float r2 = rng.UniformFloat();
        float sqrt_r1 = std::sqrt(r1);

        Vec3 p0 = t.p0;
        Vec3 p1 = t.p0 + t.e1;
        Vec3 p2 = t.p0 + t.e2;

        result.p = (1.0f - sqrt_r1) * p0 + (sqrt_r1 * (1.0f - r2)) * p1 + (sqrt_r1 * r2) * p2;
        result.n = Normalize(Cross(t.e1, t.e2));

        float area = 0.5f * Cross(t.e1, t.e2).Length();
        result.pdf = (area > 0.0f) ? 1.0f / area : 0.0f;
    }

    return result;
}

}  // namespace skwr
