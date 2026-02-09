#ifndef SKWR_SCENE_SCENE_H_
#define SKWR_SCENE_SCENE_H_

#include <sys/types.h>

#include <cstdint>
#include <vector>

#include "accelerators/bvh.h"
#include "core/constants.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "materials/material.h"

/**
 * ├── scene/               # The "World" Container
    │   ├── scene.h          # Holds: vector<Shape>, vector<Light>, BVH
    │   └── camera.h         # Camera logic
 */

namespace skwr {

// Forward declarations of pointers
struct Ray;
struct SurfaceInteraction;

class Scene {
  public:
    Scene() = default;

    // void AddShape(const Shape &shape);

    // Returns the index of the added sphere (for debugging rn)
    uint32_t AddSphere(const Sphere& s) {
        spheres_.push_back(s);
        return static_cast<uint32_t>(spheres_.size() - 1);
    }

    uint32_t AddMaterial(const Material& m) {
        materials_.push_back(m);
        return static_cast<uint32_t>(materials_.size() - 1);
    }

    const Material& GetMaterial(uint32_t id) const { return materials_[id]; }

    // Returns mesh_id (index in the meshes_ vector)
    uint32_t AddMesh(Mesh&& m) {
        meshes_.push_back(std::move(m));
        uint32_t mesh_id = (uint32_t)meshes_.size() - 1;

        // AUTO-GENERATE TRIANGLES
        // When we add a mesh, we immediately break it into Triangle primitives
        // so the renderer can see them.
        const Mesh& mesh_ref = meshes_.back();
        for (size_t i = 0; i < mesh_ref.indices.size(); i += 3) {
            Triangle t;
            t.mesh_id = mesh_id;
            t.v_idx = (uint32_t)i;  // Points to the first index of the triplet
            triangles_.push_back(t);
        }

        return mesh_id;
    }

    // We need to look up meshes by ID during intersection
    const Mesh& GetMesh(uint32_t id) const { return meshes_[id]; }

    void Build();  // Construct the BVH from the shapes list

    // THE CRITICAL HOT-PATH FUNCTION
    // The Integrator calls this millions of times.
    // rn loops through linearly, but when BVH is implemented, should be faster
    bool Intersect(const Ray& r, Float t_min, Float t_max, SurfaceInteraction* si) const;
    bool IntersectBVH(const Ray& r, Float t_min, Float t_max, SurfaceInteraction* si) const;

    // Needed for light sampling (picking a random light)
    // const std::vector<Light> &GetLights() const;

  private:
    std::vector<Sphere> spheres_;
    std::vector<Material> materials_;
    std::vector<Mesh> meshes_;
    std::vector<Triangle> triangles_;
    BVH bvh_;
};

}  // namespace skwr

#endif  // SKWR_SCENE_SCENE_H_
