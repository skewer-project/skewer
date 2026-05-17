#include <gtest/gtest.h>

#include "core/math/transform.h"
#include "core/sampling/volume_stack.h"

namespace skwr {

TEST(VolumeStackTRS, GetActiveTRSEmptyStackReturnsIdentity) {
    VolumeStack vs;
    EXPECT_TRUE(TRSIsIdentity(vs.GetActiveTRS()));
}

TEST(VolumeStackTRS, PushSetsActiveTRS) {
    VolumeStack vs;
    TRS trs = TRSFromEuler(Vec3(1.0f, 2.0f, 3.0f), Vec3(10.0f, 20.0f, 30.0f), Vec3(2.0f, 2.0f, 2.0f));
    vs.Push(1, 10, trs);
    EXPECT_EQ(vs.GetActiveMedium(), 1);
    EXPECT_NEAR(vs.GetActiveTRS().translation.x(), 1.0f, 1e-6f);
    EXPECT_NEAR(vs.GetActiveTRS().translation.y(), 2.0f, 1e-6f);
    EXPECT_NEAR(vs.GetActiveTRS().translation.z(), 3.0f, 1e-6f);
}

TEST(VolumeStackTRS, PushDefaultTRSGivesIdentity) {
    VolumeStack vs;
    vs.Push(1, 10);  // No TRS provided, defaults to identity
    EXPECT_TRUE(TRSIsIdentity(vs.GetActiveTRS()));
}

TEST(VolumeStackTRS, PushPreservesExistingTRSAfterHigherPriorityPush) {
    VolumeStack vs;
    TRS trs1 = TRSFromEuler(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS trs2 = TRSFromEuler(Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    vs.Push(1, 10, trs1);  // lower priority, deeper in stack
    vs.Push(2, 20, trs2);  // higher priority, on top
    // trs2 should be active (top of stack)
    EXPECT_EQ(vs.GetActiveMedium(), 2);
    EXPECT_NEAR(vs.GetActiveTRS().translation.y(), 2.0f, 1e-6f);
}

TEST(VolumeStackTRS, PopRestoresParentTRS) {
    VolumeStack vs;
    TRS trs1 = TRSFromEuler(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS trs2 = TRSFromEuler(Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    vs.Push(1, 10, trs1);
    vs.Push(2, 20, trs2);
    vs.Pop(2);  // pop top
    // trs1 should be active again
    EXPECT_EQ(vs.GetActiveMedium(), 1);
    EXPECT_NEAR(vs.GetActiveTRS().translation.x(), 1.0f, 1e-6f);
    EXPECT_TRUE(TRSIsIdentity(vs.GetActiveTRS()));  // trs2 should be gone
}

TEST(VolumeStackTRS, PopNonExistentDoesNotCorruptStack) {
    VolumeStack vs;
    TRS trs = TRSFromEuler(Vec3(5.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    vs.Push(1, 10, trs);
    vs.Pop(99);  // non-existent medium
    EXPECT_EQ(vs.GetActiveMedium(), 1);
    EXPECT_NEAR(vs.GetActiveTRS().translation.x(), 5.0f, 1e-6f);
}

TEST(VolumeStackTRS, MultiplePushPopPreservesCorrectTRS) {
    VolumeStack vs;
    TRS trs1 = TRSFromEuler(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS trs2 = TRSFromEuler(Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    TRS trs3 = TRSFromEuler(Vec3(0.0f, 0.0f, 3.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    vs.Push(1, 10, trs1);
    vs.Push(2, 20, trs2);
    vs.Push(3, 30, trs3);

    EXPECT_EQ(vs.GetActiveMedium(), 3);
    EXPECT_NEAR(vs.GetActiveTRS().translation.z(), 3.0f, 1e-6f);

    vs.Pop(3);
    EXPECT_EQ(vs.GetActiveMedium(), 2);
    EXPECT_NEAR(vs.GetActiveTRS().translation.y(), 2.0f, 1e-6f);

    vs.Pop(2);
    EXPECT_EQ(vs.GetActiveMedium(), 1);
    EXPECT_NEAR(vs.GetActiveTRS().translation.x(), 1.0f, 1e-6f);

    vs.Pop(1);
    EXPECT_TRUE(TRSIsIdentity(vs.GetActiveTRS()));
}

TEST(VolumeStackTRS, ContainsReturnsFalseAfterPop) {
    VolumeStack vs;
    TRS trs = TRSFromEuler(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    vs.Push(42, 10, trs);
    EXPECT_TRUE(vs.Contains(42));
    vs.Pop(42);
    EXPECT_FALSE(vs.Contains(42));
    EXPECT_TRUE(TRSIsIdentity(vs.GetActiveTRS()));
}

}  // namespace skwr