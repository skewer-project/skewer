#include "scene/scene.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "accelerators/bvh.h"
#include "core/cpu_config.h"
#include "core/math/transform.h"
#include "core/math/vec3.h"
#include "core/transport/surface_interaction.h"
#include "geometry/intersect_sphere.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "materials/material.h"
#include "media/mediums.h"
#include "media/nano_vdb_medium.h"

namespace skwr {

void Scene::MergeGraphRoots(std::vector<SceneNode>&& roots) {
    if (roots.empty()) {
        return;
    }
    if (!graph_root_) {
        SceneNode synthetic;
        synthetic.type = NodeType::Group;
        synthetic.children = std::move(roots);
        graph_root_ = std::move(synthetic);
        return;
    }
    for (auto& r : roots) {
        graph_root_->children.push_back(std::move(r));
    }
}

void Scene::FlattenGraph(const SceneNode& node, const TRS& parent_world) {
    TRS local = node.anim_transform.Evaluate(0.0f);
    TRS world = Compose(parent_world, local);

    switch (node.type) {
        case NodeType::Group:
            for (const SceneNode& ch : node.children) {
                FlattenGraph(ch, world);
            }
            break;
        case NodeType::Mesh:
            for (uint32_t mesh_id : node.mesh_ids) {
                if (TRSIsIdentity(world)) {
                    continue;
                }
                Mesh& mesh = GetMutableMesh(mesh_id);
                for (Vec3& v : mesh.p) {
                    v = TRSApplyPoint(world, v);
                }
                if (!mesh.n.empty()) {
                    for (Vec3& n : mesh.n) {
                        n = TRSApplyNormal(world, n);
                    }
                }
            }
            break;
        case NodeType::Sphere: {
            if (!node.sphere_data.has_value()) {
                throw std::runtime_error("Sphere node missing sphere_data");
            }
            const SphereData& sd = *node.sphere_data;
            if (sd.center_is_world) {
                if (!TRSIsIdentity(world)) {
                    throw std::runtime_error(
                        "Sphere with world-space center (e.g. NanoVDB) requires identity world "
                        "transform");
                }
                AddSphere(Sphere{sd.center, sd.radius, sd.material_id, sd.light_index,
                                 sd.interior_medium, sd.exterior_medium, sd.priority});
            } else {
                if (!TRSIsUniformScale(world)) {
                    throw std::runtime_error(
                        "Sphere requires uniform scale; non-uniform world scale is not supported");
                }
                float s = world.scale.x();
                Vec3 c = TRSApplyPoint(world, sd.center);
                float r = sd.radius * std::fabs(s);
                AddSphere(Sphere{c, r, sd.material_id, sd.light_index, sd.interior_medium,
                                 sd.exterior_medium, sd.priority});
            }
            break;
        }
    }
}

void Scene::Build() {
    if (graph_root_) {
        FlattenGraph(*graph_root_, TRS{});
        graph_root_.reset();
    }

    triangles_.clear();
    lights_.clear();

    // Re-register sphere lights
    for (uint32_t i = 0; i < (uint32_t)spheres_.size(); ++i) {
        spheres_[i].light_index = -1;
        if (spheres_[i].material_id == kNullMaterialId) continue;

        const Material& mat = materials_[spheres_[i].material_id];
        if (mat.IsEmissive()) {
            AreaLight light;
            light.type = AreaLight::Sphere;
            light.primitive_index = i;
            light.emission = mat.emission;
            lights_.push_back(light);
            spheres_[i].light_index = static_cast<int32_t>(lights_.size() - 1);
        }
    }

    // TODO: Update triangle creation with in/out medium setting
    // Bake one Triangle per mesh face, capturing final vertex positions,
    // edges, normals, and material_id from the fully-prepared Mesh objects.
    for (uint32_t mesh_id = 0; mesh_id < (uint32_t)meshes_.size(); ++mesh_id) {
        const Mesh& mesh_ref = meshes_[mesh_id];
        const Material* mat =
            (mesh_ref.material_id != kNullMaterialId) ? &materials_[mesh_ref.material_id] : nullptr;

        for (size_t i = 0; i < mesh_ref.indices.size(); i += 3) {
            uint32_t i0 = mesh_ref.indices[i];
            uint32_t i1 = mesh_ref.indices[i + 1];
            uint32_t i2 = mesh_ref.indices[i + 2];

            Triangle t;
            t.p0 = mesh_ref.p[i0];
            t.e1 = mesh_ref.p[i1] - t.p0;
            t.e2 = mesh_ref.p[i2] - t.p0;
            t.material_id = mesh_ref.material_id;
            t.interior_medium = kVacuumMediumId;
            t.exterior_medium = kVacuumMediumId;
            t.priority = 0;
            t.needs_tangent_frame = mat != nullptr && mat->HasNormalMap();

            if (!mesh_ref.n.empty()) {
                t.n0 = mesh_ref.n[i0];
                t.n1 = mesh_ref.n[i1];
                t.n2 = mesh_ref.n[i2];
            } else {
                Vec3 geom_n = Normalize(Cross(t.e1, t.e2));
                t.n0 = t.n1 = t.n2 = geom_n;
            }

            if (!mesh_ref.uv.empty()) {
                t.uv0 = mesh_ref.uv[i0];
                t.uv1 = mesh_ref.uv[i1];
                t.uv2 = mesh_ref.uv[i2];
            } else {
                t.uv0 = t.uv1 = t.uv2 = Vec3(0.0f, 0.0f, 0.0f);
            }

            triangles_.push_back(t);
        }
    }

    if (!triangles_.empty()) {
        std::cout << "Building BVH for " << triangles_.size() << " triangles...\n";
        bvh_.Build(triangles_);
    }

    for (uint32_t i = 0; i < (uint32_t)triangles_.size(); ++i) {
        const Material* mat = (triangles_[i].material_id != kNullMaterialId)
                                  ? &materials_[triangles_[i].material_id]
                                  : nullptr;
        if (mat != nullptr && mat->IsEmissive()) {
            AreaLight light;
            light.type = AreaLight::Triangle;
            light.primitive_index = i;
            light.emission = mat->emission;
            lights_.push_back(light);
            triangles_[i].light_index = static_cast<int32_t>(lights_.size() - 1);
        }
    }
    inv_light_count_ = lights_.empty() ? 0.0f : 1.0f / static_cast<float>(lights_.size());
}

bool Scene::Intersect(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const {
    bool hit_anything = false;
    float closest_t = t_max;
    for (const auto& sphere : spheres_) {
        if (IntersectSphere(r, sphere, t_min, closest_t, si)) {
            hit_anything = true;
            closest_t = si->t;
        }
    }

    if (bvh_.Intersect(r, t_min, closest_t, si, Triangles())) {
        hit_anything = true;
    }

    return hit_anything;
}

uint32_t Scene::AddSphere(const Sphere& s) {
    spheres_.push_back(s);
    return (uint32_t)spheres_.size() - 1;
}

uint32_t Scene::AddMaterial(const Material& m) {
    materials_.push_back(m);
    return static_cast<uint32_t>(materials_.size() - 1);
}

uint32_t Scene::AddMesh(Mesh&& m) {
    meshes_.push_back(std::move(m));
    return (uint32_t)meshes_.size() - 1;
}

uint32_t Scene::AddTexture(ImageTexture&& t) {
    textures_.push_back(std::move(t));
    return static_cast<uint32_t>(textures_.size() - 1);
}

uint16_t Scene::AddHomogeneousMedium(const HomogeneousMedium& m) {
    if (homogeneous_media_.size() >= static_cast<size_t>(kMediumIndexMask) + 1u) {
        return kVacuumMediumId;  // or assert / throw
    }
    homogeneous_media_.push_back(m);
    uint16_t index = static_cast<uint16_t>(homogeneous_media_.size() - 1);
    return PackMediumId(MediumType::Homogeneous, index);
}

uint16_t Scene::AddGridMedium(const GridMedium& m) {
    if (grid_media_.size() >= static_cast<size_t>(kMediumIndexMask) + 1u) {
        return kVacuumMediumId;  // or assert / throw
    }
    grid_media_.push_back(m);
    uint16_t index = static_cast<uint16_t>(grid_media_.size() - 1);
    return PackMediumId(MediumType::Grid, index);
}

uint16_t Scene::AddNanoVDBMedium(NanoVDBMedium m) {
    if (nanovdb_media_.size() >= static_cast<size_t>(kMediumIndexMask) + 1u) {
        return kVacuumMediumId;  // or assert / throw
    }
    nanovdb_media_.push_back(std::move(m));
    uint16_t index = static_cast<uint16_t>(nanovdb_media_.size() - 1);
    return PackMediumId(MediumType::NanoVDB, index);
}

}  // namespace skwr
