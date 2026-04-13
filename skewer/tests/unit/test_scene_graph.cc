#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "core/cpu_config.h"
#include "core/math/quat.h"
#include "core/math/transform.h"
#include "core/math/vec3.h"
#include "core/spectral/spectral_utils.h"
#include "geometry/mesh.h"
#include "io/graph_from_json.h"
#include "io/scene_loader.h"
#include "materials/material.h"
#include "scene/animation.h"
#include "scene/interp_curve.h"
#include "scene/scene.h"
#include "scene/scene_graph.h"

namespace skwr {
namespace {

struct SpectralInit {
    SpectralInit() { InitSpectralModel(); }
} g_init_spectral;

}  // namespace

using json = nlohmann::json;

TEST(GraphJson, ParseTRSDefaults) {
    json j = json::object();
    TRS t = ParseTRSFields(j);
    EXPECT_TRUE(TRSIsIdentity(t));
}

TEST(GraphJson, ParseTRSScalarScale) {
    json j = {{"translate", {1.0f, 2.0f, 3.0f}}, {"rotate", {0.0f, 90.0f, 0.0f}}, {"scale", 2.0f}};
    TRS t = ParseTRSFields(j);
    TRS ref = TRSFromEuler(Vec3(1.0f, 2.0f, 3.0f), Vec3(0.0f, 90.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));
    EXPECT_NEAR(t.translation.x(), ref.translation.x(), 1e-5f);
    EXPECT_NEAR(t.scale.x(), ref.scale.x(), 1e-5f);
    EXPECT_NEAR(QuatDot(t.rotation, ref.rotation), 1.0f, 1e-4f);
}

TEST(GraphJson, ParseCurvePresets) {
    auto lin = ParseCurveJson(json("linear"));
    EXPECT_NEAR(lin->Evaluate(0.3f), BezierCurve::Linear().Evaluate(0.3f), 1e-5f);
    auto ein = ParseCurveJson(json("ease-in"));
    EXPECT_LT(ein->Evaluate(0.1f), BezierCurve::Linear().Evaluate(0.1f));
}

TEST(GraphJson, ParseCurveBezierObject) {
    json j = {{"bezier", {0.2f, 0.5f, 0.8f, 0.6f}}};
    auto c = ParseCurveJson(j);
    EXPECT_TRUE(std::isfinite(c->Evaluate(0.4f)));
}

TEST(GraphJson, ParseCurveInvalidThrows) {
    EXPECT_THROW(ParseCurveJson(json(42)), std::runtime_error);
}

TEST(GraphJson, ParseAnimatedTransformStatic) {
    json j = {{"translate", {0.0f, 1.0f, 0.0f}}, {"scale", 2.0f}};
    AnimatedTransform a = ParseAnimatedTransformJson(j);
    ASSERT_EQ(a.keyframes.size(), 1u);
    EXPECT_NEAR(a.keyframes[0].transform.translation.y(), 1.0f, 1e-5f);
    EXPECT_NEAR(a.keyframes[0].transform.scale.x(), 2.0f, 1e-5f);
}

TEST(GraphJson, ParseAnimatedTransformKeyframes) {
    json j;
    j["keyframes"] =
        json::array({{{"time", 0.0f}, {"translate", {0.0f, 0.0f, 0.0f}}},
                     {{"time", 1.0f}, {"translate", {1.0f, 0.0f, 0.0f}}, {"curve", "linear"}}});
    AnimatedTransform a = ParseAnimatedTransformJson(j);
    ASSERT_EQ(a.keyframes.size(), 2u);
    TRS mid = a.Evaluate(0.5f);
    EXPECT_NEAR(mid.translation.x(), 0.5f, 1e-5f);
}

TEST(SceneGraph, LoadLayerQuadFromFile) {
    std::filesystem::path p =
        std::filesystem::temp_directory_path() / "skewer_ut_scene_graph_layer.json";
    {
        std::ofstream out(p);
        out << R"({
  "materials": {
    "mat": { "type": "lambertian", "albedo": [0.8, 0.8, 0.8] }
  },
  "graph": [
    {
      "type": "quad",
      "material": "mat",
      "vertices": [[0,0,0],[1,0,0],[1,1,0],[0,1,0]]
    }
  ]
})";
    }
    Scene scene;
    LoadLayerFile(p.string(), scene);
    EXPECT_EQ(scene.MeshCount(), 1u);
    scene.Build();
    EXPECT_FALSE(scene.Triangles().empty());
    std::filesystem::remove(p);
}

TEST(SceneGraph, NestedTranslateFlatten) {
    Material mat{};
    mat.type = MaterialType::Lambertian;
    mat.albedo = {{0.8f, 0.8f, 0.8f}, 1.0f};

    Scene scene;
    uint32_t mid = scene.AddMaterial(mat);

    Mesh mesh;
    mesh.material_id = mid;
    mesh.p = {Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)};
    mesh.indices = {0, 1, 2};
    uint32_t mesh_id = scene.AddMesh(std::move(mesh));

    SceneNode leaf;
    leaf.type = NodeType::Mesh;
    leaf.mesh_ids.push_back(mesh_id);
    leaf.anim_transform = ParseAnimatedTransformJson(json::object());

    SceneNode parent;
    parent.type = NodeType::Group;
    Keyframe k;
    k.time = 0.0f;
    k.transform =
        TRSFromEuler(Vec3(5.0f, 3.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    static BezierCurve kLin(0, 0, 1, 1);
    k.curve = std::shared_ptr<const InterpolationCurve>(&kLin, [](const InterpolationCurve*) {});
    parent.anim_transform.keyframes.push_back(k);
    parent.children.push_back(std::move(leaf));

    scene.MergeGraphRoots({std::move(parent)});
    scene.Build();

    ASSERT_FALSE(scene.Triangles().empty());
    const Triangle& tri = scene.Triangles()[0];
    EXPECT_NEAR(tri.p0.x(), 5.0f, 1e-4f);
    EXPECT_NEAR(tri.p0.y(), 3.0f, 1e-4f);
}

TEST(SceneGraph, SphereUniformScaleWorld) {
    Material mat{};
    mat.type = MaterialType::Lambertian;
    mat.albedo = {{0.8f, 0.8f, 0.8f}, 1.0f};
    Scene scene;
    scene.AddMaterial(mat);

    SceneNode sn;
    sn.type = NodeType::Sphere;
    SphereData sd{};
    sd.center = Vec3(0.0f, 0.0f, 0.0f);
    sd.radius = 1.0f;
    sd.material_id = 0;
    sd.interior_medium = kVacuumMediumId;
    sd.exterior_medium = kVacuumMediumId;
    sn.sphere_data = sd;

    SceneNode parent;
    parent.type = NodeType::Group;
    Keyframe k;
    k.time = 0.0f;
    k.transform =
        TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));
    static BezierCurve kLin(0, 0, 1, 1);
    k.curve = std::shared_ptr<const InterpolationCurve>(&kLin, [](const InterpolationCurve*) {});
    parent.anim_transform.keyframes.push_back(k);
    parent.children.push_back(std::move(sn));

    scene.MergeGraphRoots({std::move(parent)});
    scene.Build();

    ASSERT_EQ(scene.Spheres().size(), 1u);
    EXPECT_NEAR(scene.Spheres()[0].radius, 2.0f, 1e-4f);
}

TEST(SceneGraph, SphereNonUniformScaleThrows) {
    Material mat{};
    mat.type = MaterialType::Lambertian;
    mat.albedo = {{0.8f, 0.8f, 0.8f}, 1.0f};
    Scene scene;
    scene.AddMaterial(mat);

    SceneNode sn;
    sn.type = NodeType::Sphere;
    SphereData sd{};
    sd.center = Vec3(0.0f, 0.0f, 0.0f);
    sd.radius = 1.0f;
    sd.material_id = 0;
    sd.interior_medium = kVacuumMediumId;
    sd.exterior_medium = kVacuumMediumId;
    sn.sphere_data = sd;

    SceneNode parent;
    parent.type = NodeType::Group;
    Keyframe k;
    k.time = 0.0f;
    k.transform =
        TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 1.0f, 1.0f));
    static BezierCurve kLin(0, 0, 1, 1);
    k.curve = std::shared_ptr<const InterpolationCurve>(&kLin, [](const InterpolationCurve*) {});
    parent.anim_transform.keyframes.push_back(k);
    parent.children.push_back(std::move(sn));

    scene.MergeGraphRoots({std::move(parent)});
    EXPECT_THROW(scene.Build(), std::runtime_error);
}

TEST(SceneGraph, BuildTwiceDoesNotDoubleTransform) {
    Material mat{};
    mat.type = MaterialType::Lambertian;
    mat.albedo = {{0.8f, 0.8f, 0.8f}, 1.0f};
    Scene scene;
    scene.AddMaterial(mat);

    Mesh mesh;
    mesh.material_id = 0;
    mesh.p = {Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)};
    mesh.indices = {0, 1, 2};
    uint32_t mesh_id = scene.AddMesh(std::move(mesh));

    SceneNode leaf;
    leaf.type = NodeType::Mesh;
    leaf.mesh_ids.push_back(mesh_id);
    leaf.anim_transform = ParseAnimatedTransformJson(json::object());

    SceneNode parent;
    parent.type = NodeType::Group;
    Keyframe k;
    k.time = 0.0f;
    k.transform =
        TRSFromEuler(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    static BezierCurve kLin(0, 0, 1, 1);
    k.curve = std::shared_ptr<const InterpolationCurve>(&kLin, [](const InterpolationCurve*) {});
    parent.anim_transform.keyframes.push_back(k);
    parent.children.push_back(std::move(leaf));

    scene.MergeGraphRoots({std::move(parent)});
    scene.Build();
    float x1 = scene.Triangles()[0].p0.x();
    scene.Build();
    float x2 = scene.Triangles()[0].p0.x();
    EXPECT_NEAR(x1, x2, 1e-5f);
    EXPECT_NEAR(x1, 1.0f, 1e-4f);
}

}  // namespace skwr
