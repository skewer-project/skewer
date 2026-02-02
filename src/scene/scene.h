#ifndef SKWR_SCENE_SCENE_H_
#define SKWR_SCENE_SCENE_H_

#include <cstdint>
#include <vector>

#include "core/ray.h"
#include "geometry/sphere.h"
#include "scene/surface_interaction.h"
// #include "accelerators/bvh.h"

/**
 * ├── scene/               # The "World" Container
    │   ├── scene.h          # Holds: vector<Shape>, vector<Light>, BVH
    │   └── camera.h         # Camera logic
 */

namespace skwr {

class Scene {
  public:
    Scene() = default;

    // void AddShape(const Shape &shape);

    // Returns the index of the added sphere (for debugging rn)
    uint32_t AddSphere(const Sphere &s) {
        spheres_.push_back(s);
        return static_cast<uint32_t>(spheres_.size() - 1);
    }

    // void Build();  // Constructs the BVH from the shapes list

    // THE CRITICAL HOT-PATH FUNCTION
    // The Integrator calls this millions of times.
    // rn loops through linearly, but when BVH is implemented, should be faster
    bool Intersect(const Ray &r, Float t_min, Float t_max, SurfaceInteraction *si) const;

    // Needed for light sampling (picking a random light)
    // const std::vector<Light> &GetLights() const;

  private:
    std::vector<Sphere> spheres_;  // Raw list of spheres
    // BVH bvh_;                    // The acceleration structure
};

}  // namespace skwr

#endif  // SKWR_SCENE_SCENE_H_
