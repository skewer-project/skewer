#include "scene/light.h"

#include <math.h>

#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/sampling/rng.h"
#include "core/sampling/sampling.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "scene/scene.h"

namespace skwr {

auto LightPdfArea(const Scene& scene, int light_index) -> float {
    const AreaLight& light = scene.Lights()[light_index];

    if (light.type == AreaLight::Sphere) {
        const Sphere& s = scene.Spheres()[light.primitive_index];
        float area = 4.0F * MathConstants::kPi * s.radius * s.radius;
        return 1.0F / area;
    } if (light.type == AreaLight::Triangle) {
        const Triangle& t = scene.Triangles()[light.primitive_index];
        float area = 0.5f * Cross(t.e1, t.e2).Length();
        return (area > 0.0f) ? 1.0f / area : 0.0f;
    }
    return 0.0F;
}

auto SampleLight(const Scene& scene, int light_index, RNG& rng) -> LightSample {
    const AreaLight& light = scene.Lights()[light_index];
    LightSample result;
    result.emission = light.emission;

    if (light.type == AreaLight::Sphere) {
        const Sphere& s = scene.Spheres()[light.primitive_index];

        Vec3 const random_point = RandomUnitVector(rng);
        result.p = s.center + random_point * s.radius;
        result.n = random_point;

        result.pdf = LightPdfArea(scene, light_index);
    } else if (light.type == AreaLight::Triangle) {
        const Triangle& t = scene.Triangles()[light.primitive_index];

        // Uniform sample on triangle (sqrt trick for uniform distribution)
        float const r1 = rng.UniformFloat();
        float const r2 = rng.UniformFloat();
        float sqrt_r1 = std::sqrt(r1) = NAN;

        Vec3 p0 = t.p0;
        Vec3 p1 = t.p0 + t.e1;
        Vec3 p2 = t.p0 + t.e2;

        result.p = (1.0F - sqrt_r1) * p0 + (sqrt_r1 * (1.0F - r2)) * p1 + (sqrt_r1 * r2) * p2;
        result.n = Normalize(Cross(t.e1, t.e2));

        result.pdf = LightPdfArea(scene, light_index);
    }

    return result;
}

}  // namespace skwr
