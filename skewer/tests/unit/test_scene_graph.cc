#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/math/quat.h"
#include "core/math/transform.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/spectral/spectral_utils.h"
#include "core/transport/surface_interaction.h"
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

std::filesystem::path MakeTempTestDir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open test fixture file: " + path.string());
    }
    out << contents;
}

void ExpectRuntimeErrorContains(const std::function<void()>& fn, const std::string& expected) {
    try {
        fn();
        FAIL() << "Expected std::runtime_error containing: " << expected;
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find(expected), std::string::npos)
            << "Actual error: " << e.what();
    }
}

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

TEST(SceneLoader, ParseCameraKeyframes) {
    std::filesystem::path p =
        std::filesystem::temp_directory_path() / "skewer_ut_camera_keyframes_scene.json";
    {
        std::ofstream out(p);
        out << R"({
  "camera": {
    "look_from": [0, 0, 4],
    "look_at": [0, 0, 0],
    "vup": [0, 1, 0],
    "vfov": 50,
    "aperture_radius": 0.1,
    "focus_distance": 4,
    "keyframes": [
      { "time": 0, "look_from": [0, 0, 4] },
      { "time": 2, "look_from": [2, 0, 4], "focus_distance": 8, "aperture_radius": 0.5 }
    ]
  },
  "animation": { "start": 0, "end": 2, "fps": 24, "shutter_angle": 180 },
  "layers": ["layer.json"]
})";
    }

    SceneConfig config = LoadSceneFile(p.string());
    ASSERT_EQ(config.camera_timeline.keyframes.size(), 2u);
    EXPECT_TRUE(config.camera_timeline.IsAnimated());
    EXPECT_EQ(config.camera_timeline.keyframes[0].curve, nullptr);
    EXPECT_EQ(config.camera_timeline.keyframes[1].curve, nullptr);
    CameraState mid = config.camera_timeline.Evaluate(1.0f);
    EXPECT_NEAR(mid.look_from.x(), 1.0f, 1e-5f);
    EXPECT_NEAR(mid.look_at.z(), 0.0f, 1e-5f);
    EXPECT_NEAR(mid.focus_distance, 6.0f, 1e-5f);
    EXPECT_NEAR(mid.aperture_radius, 0.3f, 1e-5f);
    std::filesystem::remove(p);
}

TEST(SceneLoader, RejectInvalidCameraKeyframeOptics) {
    std::filesystem::path p =
        std::filesystem::temp_directory_path() / "skewer_ut_bad_camera_keyframes_scene.json";
    {
        std::ofstream out(p);
        out << R"({
  "camera": {
    "look_from": [0, 0, 4],
    "look_at": [0, 0, 0],
    "keyframes": [
      { "time": 0 },
      { "time": 1, "aperture_radius": -1 }
    ]
  },
  "layers": ["layer.json"]
})";
    }

    EXPECT_THROW(LoadSceneFile(p.string()), std::runtime_error);
    std::filesystem::remove(p);
}

TEST(SceneLoader, RejectAnimatedCameraWithoutAnimationBlock) {
    std::filesystem::path p = std::filesystem::temp_directory_path() /
                              "skewer_ut_camera_keyframes_no_animation_scene.json";
    {
        std::ofstream out(p);
        out << R"({
  "camera": {
    "look_from": [0, 0, 4],
    "look_at": [0, 0, 0],
    "keyframes": [
      { "time": 0 },
      { "time": 1, "look_from": [1, 0, 4] }
    ]
  },
  "layers": ["layer.json"]
})";
    }

    EXPECT_THROW(LoadSceneFile(p.string()), std::runtime_error);
    std::filesystem::remove(p);
}

TEST(SceneLoader, RejectMissingContextFile) {
    const auto dir = MakeTempTestDir("skewer_ut_missing_context");
    WriteFile(dir / "scene.json", R"({
  "camera": { "look_from": [0, 0, 4], "look_at": [0, 0, 0] },
  "context": ["missing_ctx.json"],
  "layers": ["layer.json"]
})");
    WriteFile(dir / "layer.json", R"({ "materials": {}, "graph": [] })");

    const SceneConfig config = LoadSceneFile((dir / "scene.json").string());
    Scene scene;
    ExpectRuntimeErrorContains([&] { LoadContextIntoScene(config.context_paths, scene); },
                               "Cannot open file:");

    std::filesystem::remove_all(dir);
}

TEST(SceneLoader, RejectMissingLayerFile) {
    Scene scene;
    ExpectRuntimeErrorContains(
        [&] { LoadLayerFile("/definitely/missing/skewer_layer.json", scene); },
        "Cannot open file:");
}

TEST(SceneLoader, RejectUnknownMaterialReference) {
    const auto dir = MakeTempTestDir("skewer_ut_unknown_material");
    WriteFile(dir / "layer.json", R"({
  "materials": {
    "mat": { "type": "lambertian", "albedo": [0.8, 0.8, 0.8] }
  },
  "graph": [
    {
      "type": "sphere",
      "material": "missing",
      "center": [0, 0, 0],
      "radius": 1
    }
  ]
})");

    Scene scene;
    ExpectRuntimeErrorContains([&] { LoadLayerFile((dir / "layer.json").string(), scene); },
                               "unknown material 'missing'");

    std::filesystem::remove_all(dir);
}

TEST(SceneLoader, RejectUnknownMediumReference) {
    const auto dir = MakeTempTestDir("skewer_ut_unknown_medium");
    WriteFile(dir / "layer.json", R"({
  "materials": {
    "nullish": { "type": "lambertian", "albedo": [0.1, 0.1, 0.1] }
  },
  "graph": [
    {
      "type": "sphere",
      "material": "null",
      "center": [0, 0, 0],
      "radius": 1,
      "inside_medium": "fog"
    }
  ]
})");

    Scene scene;
    ExpectRuntimeErrorContains([&] { LoadLayerFile((dir / "layer.json").string(), scene); },
                               "Unknown medium 'fog'");

    std::filesystem::remove_all(dir);
}

TEST(SceneLoader, RejectInvalidRenderIntegrator) {
    const auto dir = MakeTempTestDir("skewer_ut_invalid_integrator");
    WriteFile(dir / "layer.json", R"({
  "materials": {},
  "graph": [],
  "render": {
    "integrator": "preview-only"
  }
})");

    Scene scene;
    ExpectRuntimeErrorContains([&] { LoadLayerFile((dir / "layer.json").string(), scene); },
                               "Unknown integrator type: preview-only");

    std::filesystem::remove_all(dir);
}

TEST(SceneLoader, RejectMissingObjFile) {
    const auto dir = MakeTempTestDir("skewer_ut_missing_obj");
    WriteFile(dir / "layer.json", R"({
  "materials": {
    "mat": { "type": "lambertian", "albedo": [0.8, 0.8, 0.8] }
  },
  "graph": [
    {
      "type": "obj",
      "material": "mat",
      "file": "missing.obj"
    }
  ]
})");

    Scene scene;
    ExpectRuntimeErrorContains([&] { LoadLayerFile((dir / "layer.json").string(), scene); },
                               "failed to load OBJ");

    std::filesystem::remove_all(dir);
}

TEST(SceneLoader, RejectMissingSkyboxFaceTexture) {
    const auto dir = MakeTempTestDir("skewer_ut_missing_skybox_face");
    WriteFile(dir / "scene.json", R"({
  "camera": { "look_from": [0, 0, 4], "look_at": [0, 0, 0] },
  "layers": ["layer.json"],
  "skybox": {
    "min": [-1, -1, -1],
    "max": [1, 1, 1],
    "faces": { "+x": "missing.exr" }
  }
})");

    ExpectRuntimeErrorContains([&] { LoadSceneFile((dir / "scene.json").string()); },
                               "Failed to load skybox texture:");

    std::filesystem::remove_all(dir);
}

TEST(SceneLoader, RejectPreviewerRepairableSkyboxBounds) {
    const auto dir = MakeTempTestDir("skewer_ut_previewer_repairable_skybox");
    WriteFile(dir / "layer.json", R"({
    "materials": {
        "mat": { "type": "lambertian", "albedo": [0.8, 0.8, 0.8] }
    },
    "graph": []
    })");
    WriteFile(dir / "ignored.ppm", R"(P3
    1 1
    255
    0 0 0
    )");
    WriteFile(dir / "scene.json", R"({
  "camera": { "look_from": [0, 0, 4], "look_at": [0, 0, 0] },
  "layers": ["layer.json"],
  "skybox": {
    "min": [2, 0, 4],
    "max": [-2, 0, 1],
    "faces": { "+x": "ignored.exr" }
  }
})");

    ExpectRuntimeErrorContains([&] { LoadSceneFile((dir / "scene.json").string()); },
                               "skybox 'max' must be greater than 'min' on every axis");

    std::filesystem::remove_all(dir);
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
    Ray r(Vec3(0.5f, 0.5f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), 0.0f);
    SurfaceInteraction si{};
    EXPECT_TRUE(
        scene.Intersect(r, RenderConstants::kRayOffsetEpsilon, MathConstants::kFloatInfinity, &si));
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

    Ray r(Vec3(5.33f, 3.33f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), 0.0f);
    SurfaceInteraction si{};
    ASSERT_TRUE(
        scene.Intersect(r, RenderConstants::kRayOffsetEpsilon, MathConstants::kFloatInfinity, &si));
    EXPECT_NEAR(si.point.x(), 5.33f, 0.02f);
    EXPECT_NEAR(si.point.y(), 3.33f, 0.02f);
    EXPECT_NEAR(si.point.z(), 0.0f, 1e-3f);
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

TEST(SceneGraph, BuildSingleTransformHitPosition) {
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
    Ray r(Vec3(1.33f, 0.33f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), 0.0f);
    SurfaceInteraction si{};
    ASSERT_TRUE(
        scene.Intersect(r, RenderConstants::kRayOffsetEpsilon, MathConstants::kFloatInfinity, &si));
    EXPECT_NEAR(si.point.x(), 1.33f, 0.02f);
    EXPECT_NEAR(si.point.y(), 0.33f, 0.02f);
}

}  // namespace skwr
