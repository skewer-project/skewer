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
#include "media/nano_vdb_medium.h"
#include "scene/light.h"
#include "io/scene_loader.h"
#include <map>
#include <string>

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
    uint16_t AddGridMedium(const GridMedium& m);
    uint16_t AddNanoVDBMedium(NanoVDBMedium m);  // Move f(std::move(m)) or  Pass by copy f(m)
    void SetGlobalMedium(uint16_t medium_id) { global_medium_id_ = medium_id; }
    uint16_t GetGlobalMedium() const { return global_medium_id_; }

    void SetNodes(const std::vector<SceneNode>& nodes) {
        nodes_.clear();
        node_id_to_string_.clear();
        node_string_to_id_.clear();
        for (const auto& n : nodes) {
            nodes_[n.id] = n;
            node_string_to_id_[n.id] = (int32_t)node_id_to_string_.size();
            node_id_to_string_.push_back(n.id);
        }
    }

    const std::map<std::string, SceneNode>& Nodes() const { return nodes_; }
    const std::string& GetNodeStringId(int32_t id) const { return node_id_to_string_[id]; }

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
    const std::vector<GridMedium>& grid_media() const { return grid_media_; }
    const std::vector<NanoVDBMedium>& nanovdb_media() const { return nanovdb_media_; }
    const float& InvLightCount() const { return inv_light_count_; }

    void Build(float t_start = 0.0f, float t_end = 0.0f);  // Construct the BVH from the shapes list

    // THE CRITICAL HOT-PATH FUNCTION
    // The Integrator calls this millions of times.
    // rn loops through linearly, but when BVH is implemented, should be faster
    bool Intersect(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const;

  private:
    std::vector<Sphere> spheres_;
    std::vector<Material> materials_;
    std::vector<ImageTexture> textures_;
    std::vector<Mesh> meshes_;
    std::vector<Triangle> triangles_;
    std::vector<AreaLight> lights_;
    std::vector<HomogeneousMedium> homogeneous_media_;
    std::vector<GridMedium> grid_media_;
    std::vector<NanoVDBMedium> nanovdb_media_;
    BVH bvh_;
    float inv_light_count_;
    uint16_t global_medium_id_ = 0;  // 0 represents Vacuum

    std::map<std::string, SceneNode> nodes_;
    std::vector<std::string> node_id_to_string_;
    std::map<std::string, int32_t> node_string_to_id_;
};

}  // namespace skwr

#endif  // SKWR_SCENE_SCENE_H_
