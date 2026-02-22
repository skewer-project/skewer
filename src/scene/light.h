#ifndef SKWR_SCENE_LIGHT_H_
#define SKWR_SCENE_LIGHT_H_

#include "core/rng.h"
#include "core/spectrum.h"
#include "core/vec3.h"

namespace skwr {

class Scene;

// A lightweight reference to an emissive primitive in the Scene
struct AreaLight {
    enum Type { Sphere, Triangle } type;
    uint32_t primitive_index;  // Index into scene.spheres_ or scene.meshes_
    Spectrum emission;         // cache the emission
    // BoundBox bounds;           // Bounding Box for optimization
};

struct LightSample {
    Vec3 p;             // Point on the light
    Vec3 n;             // Normal at that point
    Spectrum emission;  // Radiance (Le) or color
    float pdf;          // Probability density = (1 / Area)
};

// Returns a random point on the surface of the light
LightSample SampleLight(const Scene& scene, const AreaLight& light, RNG& rng);

}  // namespace skwr

#endif  // SKWR_SCENE_LIGHT_H_
