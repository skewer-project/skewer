#include <gtest/gtest.h>

#include "accelerators/blas.h"
#include "accelerators/instance.h"
#include "accelerators/tlas.h"
#include "core/cpu_config.h"
#include "core/math/transform.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/transport/surface_interaction.h"
#include "geometry/triangle.h"
#include "scene/animation.h"

namespace skwr {

TEST(TLAS, SingleStaticInstanceMatchesDirectBvh) {
    Triangle tri{};
    tri.p0 = Vec3(0.0f, 0.0f, 0.0f);
    tri.e1 = Vec3(1.0f, 0.0f, 0.0f);
    tri.e2 = Vec3(0.0f, 1.0f, 0.0f);
    tri.n0 = tri.n1 = tri.n2 = Vec3(0.0f, 0.0f, 1.0f);
    tri.uv0 = tri.uv1 = tri.uv2 = Vec3(0.0f, 0.0f, 0.0f);
    tri.material_id = kNullMaterialId;

    std::vector<Triangle> tris{tri};
    BLAS blas;
    blas.triangles = tris;
    blas.local_bounds = BoundBox(Vec3(0, 0, 0), Vec3(1, 1, 0));
    blas.local_bounds.PadToMinimums();
    blas.bvh.Build(blas.triangles);
    std::vector<BLAS> blases;
    blases.push_back(std::move(blas));

    AnimatedTransform id{};
    Instance inst;
    inst.blas_id = 0;
    inst.transform_chain = {id};
    inst.is_static = true;
    inst.static_world_from_local = EvaluateTransformChain(inst.transform_chain, 0.0f);
    inst.world_bounds = TransformBounds(inst.static_world_from_local, blases[0].local_bounds);
    inst.tri_light_indices.assign(blases[0].triangles.size(), -1);

    std::vector<Instance> instances{inst};
    TLAS tlas;
    tlas.Build(instances);

    Ray r(Vec3(0.25f, 0.25f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), 0.0f);
    SurfaceInteraction si_tlas{};
    ASSERT_TRUE(tlas.Intersect(r, 1e-4f, 1e10f, &si_tlas, blases, instances));

    SurfaceInteraction si_bvh{};
    ASSERT_TRUE(blases[0].bvh.Intersect(r, 1e-4f, 1e10f, &si_bvh, blases[0].triangles));

    EXPECT_NEAR(si_tlas.t, si_bvh.t, 1e-4f);
    EXPECT_NEAR(si_tlas.point.x(), si_bvh.point.x(), 1e-4f);
    EXPECT_NEAR(si_tlas.point.y(), si_bvh.point.y(), 1e-4f);
}

TEST(TLAS, AnimatedInstanceMovesWithRayTime) {
    Triangle tri{};
    tri.p0 = Vec3(0.0f, 0.0f, 0.0f);
    tri.e1 = Vec3(0.2f, 0.0f, 0.0f);
    tri.e2 = Vec3(0.0f, 0.2f, 0.0f);
    tri.n0 = tri.n1 = tri.n2 = Vec3(0.0f, 0.0f, 1.0f);
    tri.uv0 = tri.uv1 = tri.uv2 = Vec3(0.0f, 0.0f, 0.0f);
    tri.material_id = kNullMaterialId;

    BLAS blas;
    blas.triangles = {tri};
    blas.local_bounds = BoundBox(Vec3(0, 0, 0), Vec3(0.2f, 0.2f, 0.0f));
    blas.local_bounds.PadToMinimums();
    blas.bvh.Build(blas.triangles);
    std::vector<BLAS> blases;
    blases.push_back(std::move(blas));

    AnimatedTransform anim;
    Keyframe k0;
    k0.time = 0.0f;
    k0.transform.translation = Vec3(0.0f, 0.0f, 0.0f);
    Keyframe k1;
    k1.time = 1.0f;
    k1.transform.translation = Vec3(2.0f, 0.0f, 0.0f);
    anim.keyframes = {k0, k1};

    Instance inst;
    inst.blas_id = 0;
    inst.transform_chain = {anim};
    inst.is_static = false;
    inst.static_world_from_local = {};
    BoundBox lb = blases[0].local_bounds;
    TRS a = EvaluateTransformChain(inst.transform_chain, 0.0f);
    TRS b = EvaluateTransformChain(inst.transform_chain, 1.0f);
    inst.world_bounds = Union(TransformBounds(a, lb), TransformBounds(b, lb));
    inst.tri_light_indices.assign(blases[0].triangles.size(), -1);

    std::vector<Instance> instances{inst};
    TLAS tlas;
    tlas.Build(instances);

    Ray r0(Vec3(0.1f, 0.1f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), 0.0f);
    SurfaceInteraction si0{};
    ASSERT_TRUE(tlas.Intersect(r0, 1e-4f, 1e10f, &si0, blases, instances));
    EXPECT_NEAR(si0.point.x(), 0.1f, 0.02f);

    Ray r1(Vec3(2.1f, 0.1f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), 1.0f);
    SurfaceInteraction si1{};
    ASSERT_TRUE(tlas.Intersect(r1, 1e-4f, 1e10f, &si1, blases, instances));
    EXPECT_NEAR(si1.point.x(), 2.1f, 0.02f);
}

}  // namespace skwr
