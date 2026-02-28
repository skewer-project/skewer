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
#include "materials/texture.h"
#include "media/mediums.h"
#include "scene/light.h"

namespace skwr {

// Forward declarations of pointers
class Ray;
struct SurfaceInteraction;

class Scene {
  public:
    Scene() = default;

    uint32_t AddSphere(const Sphere& s);
    uint32_t AddMaterial(const Material& m);
    uint32_t AddMesh(Mesh&& m);             // Returns mesh_id (index in the meshes_ vector)
    uint32_t AddTexture(ImageTexture&& t);  // Returns texture_id
    uint16_t AddHomogeneousMedium(const HomogeneousMedium& m);

    const Material& GetMaterial(uint32_t id) const { return materials_[id]; }
    const ImageTexture& GetTexture(uint32_t id) const { return textures_[id]; }
    const Mesh& GetMesh(uint32_t id) const { return meshes_[id]; }
    Mesh& GetMutableMesh(uint32_t id) { return meshes_[id]; }
    size_t MeshCount() const { return meshes_.size(); }
    const std::vector<Sphere>& Spheres() const { return spheres_; }
    const std::vector<Triangle>& Triangles() const { return triangles_; }
    const std::vector<Material>& Materials() const { return materials_; }
    const std::vector<AreaLight>& Lights() const { return lights_; }
    const std::vector<HomogeneousMedium>& homogeneous_media() const { return homogeneous_media_; }
    const float& InvLightCount() const { return inv_light_count_; }

    void Build();  // Construct the BVH from the shapes list

    // THE CRITICAL HOT-PATH FUNCTION
    // The Integrator calls this millions of times.
    // rn loops through linearly, but when BVH is implemented, should be faster
    bool Intersect(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const;
    bool IntersectBVH(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const;

  private:
    std::vector<Sphere> spheres_;
    std::vector<Material> materials_;
    std::vector<ImageTexture> textures_;
    std::vector<Mesh> meshes_;
    std::vector<Triangle> triangles_;
    std::vector<AreaLight> lights_;
    std::vector<HomogeneousMedium> homogeneous_media_;
    // std::vector<GridMedium> grid_media_;
    BVH bvh_;
    float inv_light_count_;
};

}  // namespace skwr

#endif  // SKWR_SCENE_SCENE_H_
