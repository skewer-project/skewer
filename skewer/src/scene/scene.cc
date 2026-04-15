#include "scene/scene.h"

#include <cmath>
#include <cstdint>
#include <iostream>
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

namespace {

Triangle TransformTriangleToWorld(const TRS& trs, const Triangle& L) {
    Triangle W;
    W.p0 = TRSApplyPoint(trs, L.p0);
    Vec3 p1 = TRSApplyPoint(trs, L.p0 + L.e1);
    Vec3 p2 = TRSApplyPoint(trs, L.p0 + L.e2);
    W.e1 = p1 - W.p0;
    W.e2 = p2 - W.p0;
    W.n0 = TRSApplyNormal(trs, L.n0);
    W.n1 = TRSApplyNormal(trs, L.n1);
    W.n2 = TRSApplyNormal(trs, L.n2);
    W.uv0 = L.uv0;
    W.uv1 = L.uv1;
    W.uv2 = L.uv2;
    W.material_id = L.material_id;
    W.light_index = -1;
    W.interior_medium = L.interior_medium;
    W.exterior_medium = L.exterior_medium;
    W.priority = L.priority;
    W.needs_tangent_frame = L.needs_tangent_frame;
    return W;
}

}  // namespace

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

uint32_t Scene::EnsureBlasForMesh(uint32_t mesh_id,
                                  std::unordered_map<uint32_t, uint32_t>& mesh_to_blas) {
    auto it = mesh_to_blas.find(mesh_id);
    if (it != mesh_to_blas.end()) {
        return it->second;
    }

    const Mesh& mesh_ref = meshes_[mesh_id];
    std::vector<Triangle> local_tris;
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

        local_tris.push_back(t);
    }

    BLAS blas;
    blas.triangles = std::move(local_tris);
    blas.local_bounds = BoundBox();
    for (const auto& tri : blas.triangles) {
        blas.local_bounds.Expand(tri.p0);
        blas.local_bounds.Expand(tri.p0 + tri.e1);
        blas.local_bounds.Expand(tri.p0 + tri.e2);
    }
    if (!blas.triangles.empty()) {
        blas.local_bounds.PadToMinimums();
        blas.bvh.Build(blas.triangles);
    }

    uint32_t id = static_cast<uint32_t>(blases_.size());
    blases_.push_back(std::move(blas));
    mesh_to_blas[mesh_id] = id;
    return id;
}

void Scene::ExtractInstancesFromGraph(const SceneNode& node,
                                      std::vector<AnimatedTransform> prefix) {
    std::vector<AnimatedTransform> chain = prefix;
    chain.push_back(node.anim_transform);

    switch (node.type) {
        case NodeType::Group:
            for (const SceneNode& ch : node.children) {
                ExtractInstancesFromGraph(ch, chain);
            }
            break;
        case NodeType::Mesh: {
            std::unordered_map<uint32_t, uint32_t> mesh_to_blas;
            for (uint32_t mesh_id : node.mesh_ids) {
                uint32_t blas_id = EnsureBlasForMesh(mesh_id, mesh_to_blas);
                if (blases_[blas_id].triangles.empty()) {
                    continue;
                }
                Instance inst;
                inst.blas_id = blas_id;
                inst.transform_chain = chain;
                inst.is_static = TransformChainIsStatic(chain);
                inst.static_world_from_local =
                    inst.is_static ? EvaluateTransformChain(chain, 0.0f) : TRS{};
                const BoundBox& lb = blases_[blas_id].local_bounds;
                if (inst.is_static) {
                    inst.world_bounds = TransformBounds(inst.static_world_from_local, lb);
                } else {
                    TRS a = EvaluateTransformChain(chain, shutter_open_);
                    TRS b = EvaluateTransformChain(chain, shutter_close_);
                    inst.world_bounds = Union(TransformBounds(a, lb), TransformBounds(b, lb));
                }
                inst.tri_light_indices.assign(blases_[blas_id].triangles.size(), -1);
                instances_.push_back(std::move(inst));
            }
            break;
        }
        case NodeType::Sphere: {
            if (!node.sphere_data.has_value()) {
                throw std::runtime_error("Sphere node missing sphere_data");
            }
            const SphereData& sd = *node.sphere_data;
            if (sd.center_is_world) {
                TRS w = EvaluateTransformChain(chain, 0.0f);
                if (!TRSIsIdentity(w)) {
                    throw std::runtime_error(
                        "Sphere with world-space center (e.g. NanoVDB) requires identity world "
                        "transform");
                }
                AddSphere(Sphere{sd.center, sd.radius, sd.material_id, sd.light_index,
                                 sd.interior_medium, sd.exterior_medium, sd.priority});
            } else if (TransformChainIsStatic(chain)) {
                TRS w = EvaluateTransformChain(chain, 0.0f);
                if (!TRSIsUniformScale(w)) {
                    throw std::runtime_error(
                        "Sphere requires uniform scale; non-uniform world scale is not supported");
                }
                float sc = w.scale.x();
                Vec3 c = TRSApplyPoint(w, sd.center);
                float r = sd.radius * std::fabs(sc);
                AddSphere(Sphere{c, r, sd.material_id, sd.light_index, sd.interior_medium,
                                 sd.exterior_medium, sd.priority});
            } else {
                AnimatedSphere asphere;
                asphere.local_data = sd;
                asphere.transform_chain = chain;
                animated_spheres_.push_back(std::move(asphere));
            }
            break;
        }
    }
}

void Scene::BuildLegacyMeshBvhAndLights() {
    for (uint32_t mesh_id = 0; mesh_id < static_cast<uint32_t>(meshes_.size()); ++mesh_id) {
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

    for (uint32_t i = 0; i < static_cast<uint32_t>(triangles_.size()); ++i) {
        const Material* mat = (triangles_[i].material_id != kNullMaterialId)
                                  ? &materials_[triangles_[i].material_id]
                                  : nullptr;
        if (mat != nullptr && mat->IsEmissive()) {
            Triangle lt = triangles_[i];
            uint32_t light_idx = static_cast<uint32_t>(lights_.size());
            lt.light_index = static_cast<int32_t>(light_idx);
            light_triangles_.push_back(lt);
            AreaLight light;
            light.type = AreaLight::Triangle;
            light.primitive_index = static_cast<uint32_t>(light_triangles_.size() - 1);
            light.emission = mat->emission;
            lights_.push_back(light);
            triangles_[i].light_index = static_cast<int32_t>(light_idx);
        }
    }
}

void Scene::Build() {
    triangles_.clear();
    lights_.clear();
    light_triangles_.clear();
    light_spheres_.clear();
    inv_light_count_ = 0.0f;
    instances_.clear();
    blases_.clear();
    tlas_ = TLAS{};
    bvh_ = BVH{};
    animated_spheres_.clear();

    if (graph_root_) {
        spheres_.clear();
        ExtractInstancesFromGraph(*graph_root_, {});
        graph_root_.reset();

        float mid_t = 0.5f * (shutter_open_ + shutter_close_);

        for (uint32_t i = 0; i < static_cast<uint32_t>(spheres_.size()); ++i) {
            spheres_[i].light_index = -1;
            if (spheres_[i].material_id == kNullMaterialId) {
                continue;
            }
            const Material& mat = materials_[spheres_[i].material_id];
            if (mat.IsEmissive()) {
                light_spheres_.push_back(spheres_[i]);
                AreaLight light;
                light.type = AreaLight::Sphere;
                light.primitive_index = static_cast<uint32_t>(light_spheres_.size() - 1);
                light.emission = mat.emission;
                lights_.push_back(light);
                int32_t li = static_cast<int32_t>(lights_.size() - 1);
                light_spheres_.back().light_index = li;
                spheres_[i].light_index = li;
            }
        }

        for (AnimatedSphere& as : animated_spheres_) {
            Sphere s_mid = as.EvaluateAt(mid_t);
            if (s_mid.material_id == kNullMaterialId) {
                continue;
            }
            const Material& mat = materials_[s_mid.material_id];
            if (mat.IsEmissive()) {
                light_spheres_.push_back(s_mid);
                AreaLight light;
                light.type = AreaLight::Sphere;
                light.primitive_index = static_cast<uint32_t>(light_spheres_.size() - 1);
                light.emission = mat.emission;
                lights_.push_back(light);
                as.emissive_light_index = static_cast<int32_t>(lights_.size() - 1);
                light_spheres_.back().light_index = as.emissive_light_index;
            }
        }

        for (Instance& inst : instances_) {
            inst.first_light_index = static_cast<uint32_t>(lights_.size());
            inst.light_count = 0;
            const BLAS& blas = blases_[inst.blas_id];
            TRS mid = inst.is_static ? inst.static_world_from_local
                                     : EvaluateTransformChain(inst.transform_chain, mid_t);
            for (size_t ti = 0; ti < blas.triangles.size(); ++ti) {
                const Triangle& lt = blas.triangles[ti];
                if (lt.material_id == kNullMaterialId) {
                    continue;
                }
                const Material& mat = materials_[lt.material_id];
                if (!mat.IsEmissive()) {
                    continue;
                }
                Triangle wt = TransformTriangleToWorld(mid, lt);
                uint32_t li = static_cast<uint32_t>(lights_.size());
                wt.light_index = static_cast<int32_t>(li);
                light_triangles_.push_back(wt);
                AreaLight L;
                L.type = AreaLight::Triangle;
                L.primitive_index = static_cast<uint32_t>(light_triangles_.size() - 1);
                L.emission = mat.emission;
                lights_.push_back(L);
                inst.tri_light_indices[ti] = static_cast<int32_t>(li);
                inst.light_count++;
            }
        }

        if (!instances_.empty()) {
            std::cout << "Building TLAS for " << instances_.size() << " instances...\n";
            tlas_.Build(instances_);
        }
    } else {
        for (uint32_t i = 0; i < static_cast<uint32_t>(spheres_.size()); ++i) {
            spheres_[i].light_index = -1;
            if (spheres_[i].material_id == kNullMaterialId) {
                continue;
            }
            const Material& mat = materials_[spheres_[i].material_id];
            if (mat.IsEmissive()) {
                light_spheres_.push_back(spheres_[i]);
                AreaLight light;
                light.type = AreaLight::Sphere;
                light.primitive_index = static_cast<uint32_t>(light_spheres_.size() - 1);
                light.emission = mat.emission;
                lights_.push_back(light);
                int32_t li = static_cast<int32_t>(lights_.size() - 1);
                light_spheres_.back().light_index = li;
                spheres_[i].light_index = li;
            }
        }
        BuildLegacyMeshBvhAndLights();
    }

    inv_light_count_ = lights_.empty() ? 0.0f : 1.0f / static_cast<float>(lights_.size());
}

bool Scene::Intersect(const Ray& r, float t_min, float t_max, SurfaceInteraction* si) const {
    bool hit_anything = false;
    float closest_t = t_max;

    for (const AnimatedSphere& asphere : animated_spheres_) {
        Sphere s = asphere.EvaluateAt(r.time());
        if (IntersectSphere(r, s, t_min, closest_t, si)) {
            hit_anything = true;
            closest_t = si->t;
        }
    }

    for (const Sphere& sphere : spheres_) {
        if (IntersectSphere(r, sphere, t_min, closest_t, si)) {
            hit_anything = true;
            closest_t = si->t;
        }
    }

    if (!tlas_.IsEmpty()) {
        if (tlas_.Intersect(r, t_min, closest_t, si, blases_, instances_)) {
            hit_anything = true;
        }
    } else if (!bvh_.IsEmpty()) {
        if (bvh_.Intersect(r, t_min, closest_t, si, triangles_)) {
            hit_anything = true;
        }
    }

    return hit_anything;
}

uint32_t Scene::AddSphere(const Sphere& s) {
    spheres_.push_back(s);
    return static_cast<uint32_t>(spheres_.size() - 1);
}

uint32_t Scene::AddMaterial(const Material& m) {
    materials_.push_back(m);
    return static_cast<uint32_t>(materials_.size() - 1);
}

uint32_t Scene::AddMesh(Mesh&& m) {
    meshes_.push_back(std::move(m));
    return static_cast<uint32_t>(meshes_.size() - 1);
}

uint32_t Scene::AddTexture(ImageTexture&& t) {
    textures_.push_back(std::move(t));
    return static_cast<uint32_t>(textures_.size() - 1);
}

uint16_t Scene::AddHomogeneousMedium(const HomogeneousMedium& m) {
    if (homogeneous_media_.size() >= static_cast<size_t>(kMediumIndexMask) + 1u) {
        return kVacuumMediumId;
    }
    homogeneous_media_.push_back(m);
    uint16_t index = static_cast<uint16_t>(homogeneous_media_.size() - 1);
    return PackMediumId(MediumType::Homogeneous, index);
}

uint16_t Scene::AddGridMedium(const GridMedium& m) {
    if (grid_media_.size() >= static_cast<size_t>(kMediumIndexMask) + 1u) {
        return kVacuumMediumId;
    }
    grid_media_.push_back(m);
    uint16_t index = static_cast<uint16_t>(grid_media_.size() - 1);
    return PackMediumId(MediumType::Grid, index);
}

uint16_t Scene::AddNanoVDBMedium(NanoVDBMedium m) {
    if (nanovdb_media_.size() >= static_cast<size_t>(kMediumIndexMask) + 1u) {
        return kVacuumMediumId;
    }
    nanovdb_media_.push_back(std::move(m));
    uint16_t index = static_cast<uint16_t>(nanovdb_media_.size() - 1);
    return PackMediumId(MediumType::NanoVDB, index);
}

}  // namespace skwr
