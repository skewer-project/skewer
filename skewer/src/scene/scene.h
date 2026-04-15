#ifndef SKWR_SCENE_SCENE_H_
#define SKWR_SCENE_SCENE_H_

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "accelerators/blas.h"
#include "accelerators/bvh.h"
#include "accelerators/instance.h"
#include "accelerators/tlas.h"
#include "geometry/animated_sphere.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "materials/material.h"
#include "materials/texture.h"
#include "media/mediums.h"
#include "media/nano_vdb_medium.h"
#include "scene/light.h"
#include "scene/scene_graph.h"

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

    const Material& GetMaterial(uint32_t id) const { return materials_[id]; }
    const ImageTexture& GetTexture(uint32_t id) const { return textures_[id]; }
    const Mesh& GetMesh(uint32_t id) const { return meshes_[id]; }
    Mesh& GetMutableMesh(uint32_t id) { return meshes_[id]; }
    size_t MeshCount() const { return meshes_.size(); }
    const std::vector<Sphere>& Spheres() const { return spheres_; }
    const std::vector<Triangle>& Triangles() const { return triangles_; }
    const std::vector<Triangle>& LightTriangles() const { return light_triangles_; }
    const std::vector<Sphere>& LightSpheres() const { return light_spheres_; }
    const std::vector<Material>& Materials() const { return materials_; }
    const std::vector<AreaLight>& Lights() const { return lights_; }
    const std::vector<HomogeneousMedium>& homogeneous_media() const { return homogeneous_media_; }
    const std::vector<GridMedium>& grid_media() const { return grid_media_; }
    const std::vector<NanoVDBMedium>& nanovdb_media() const { return nanovdb_media_; }
    const float& InvLightCount() const { return inv_light_count_; }

    void SetShutter(float open, float close) {
        shutter_open_ = open;
        shutter_close_ = close;
    }

    void Build();

    void MergeGraphRoots(std::vector<SceneNode>&& roots);

    bool HasGraph() const { return graph_root_.has_value(); }

    bool Intersect(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const;

  private:
    void ExtractInstancesFromGraph(const SceneNode& node, std::vector<AnimatedTransform> prefix);
    uint32_t EnsureBlasForMesh(uint32_t mesh_id,
                               std::unordered_map<uint32_t, uint32_t>& mesh_to_blas);
    void BuildLegacyMeshBvhAndLights();

    std::optional<SceneNode> graph_root_;
    std::vector<Sphere> spheres_;
    std::vector<AnimatedSphere> animated_spheres_;
    std::vector<Material> materials_;
    std::vector<ImageTexture> textures_;
    std::vector<Mesh> meshes_;
    std::vector<Triangle> triangles_;
    std::vector<Triangle> light_triangles_;
    std::vector<Sphere> light_spheres_;
    std::vector<AreaLight> lights_;
    std::vector<HomogeneousMedium> homogeneous_media_;
    std::vector<GridMedium> grid_media_;
    std::vector<NanoVDBMedium> nanovdb_media_;
    std::vector<BLAS> blases_;
    std::vector<Instance> instances_;
    TLAS tlas_;
    BVH bvh_;
    float inv_light_count_ = 0.0f;
    float shutter_open_ = 0.0f;
    float shutter_close_ = 0.0f;
    uint16_t global_medium_id_ = 0;  // 0 represents Vacuum
};

}  // namespace skwr

#endif  // SKWR_SCENE_SCENE_H_
