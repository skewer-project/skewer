#ifndef SKWR_SCENE_SCENE_H_
#define SKWR_SCENE_SCENE_H_

#include <sys/types.h>

#include <cstdint>
#include <vector>

#include "accelerators/bvh.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "materials/material.h"
#include "scene/light.h"

namespace skwr {

// Forward declarations of pointers
struct Ray;
struct SurfaceInteraction;

class Scene {
  public:
    Scene() = default;

    uint32_t AddSphere(const Sphere& s);
    uint32_t AddMaterial(const Material& m);
    uint32_t AddMesh(Mesh&& m);  // Returns mesh_id (index in the meshes_ vector)

    const Material& GetMaterial(uint32_t id) const { return materials_[id]; }
    const Mesh& GetMesh(uint32_t id) const { return meshes_[id]; }
    Mesh& GetMutableMesh(uint32_t id) { return meshes_[id]; }
    size_t MeshCount() const { return meshes_.size(); }
    const std::vector<Sphere>& Spheres() const { return spheres_; }
    const std::vector<Triangle>& Triangles() const { return triangles_; }
    const std::vector<Material>& Materials() const { return materials_; }
    const std::vector<AreaLight>& Lights() const { return lights_; }

    void Build();  // Construct the BVH from the shapes list

    // THE CRITICAL HOT-PATH FUNCTION
    // The Integrator calls this millions of times.
    // rn loops through linearly, but when BVH is implemented, should be faster
    bool Intersect(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const;
    bool IntersectBVH(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const;

  private:
    std::vector<Sphere> spheres_;
    std::vector<Material> materials_;
    std::vector<Mesh> meshes_;
    std::vector<Triangle> triangles_;
    std::vector<AreaLight> lights_;
    BVH bvh_;
};

}  // namespace skwr

#endif  // SKWR_SCENE_SCENE_H_
