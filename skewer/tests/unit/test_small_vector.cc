#include <gtest/gtest.h>

#include <utility>

#include "core/containers/small_vector.h"

namespace skwr {

TEST(SmallVectorTest, EmptyOnConstruction) {
    SmallVector<int, 4> v;
    EXPECT_EQ(v.size(), 0u);
    EXPECT_TRUE(v.empty());
}

TEST(SmallVectorTest, PushBelowInlineCapacityStaysInline) {
    SmallVector<int, 4> v;
    for (int i = 0; i < 4; ++i) v.push_back(i * 10);

    EXPECT_EQ(v.size(), 4u);
    EXPECT_FALSE(v.empty());
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(v[i], i * 10);
    }
}

TEST(SmallVectorTest, PushBeyondInlineSpillsToHeap) {
    SmallVector<int, 4> v;
    for (int i = 0; i < 20; ++i) v.push_back(i);

    EXPECT_EQ(v.size(), 20u);
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(v[i], i);
    }
}

TEST(SmallVectorTest, HeapGrowsPreservesElementsAcrossReallocations) {
    SmallVector<int, 2> v;  // small inline cap to force multiple grows
    constexpr int kCount = 100;
    for (int i = 0; i < kCount; ++i) v.push_back(i);

    EXPECT_EQ(v.size(), static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(v[i], i) << "element corrupted across heap regrow at i=" << i;
    }
}

TEST(SmallVectorTest, ClearReleasesAndResetsSize) {
    SmallVector<int, 2> v;
    for (int i = 0; i < 10; ++i) v.push_back(i);
    EXPECT_EQ(v.size(), 10u);

    v.clear();
    EXPECT_EQ(v.size(), 0u);
    EXPECT_TRUE(v.empty());

    // After clear, container must still be usable.
    for (int i = 0; i < 5; ++i) v.push_back(i + 100);
    EXPECT_EQ(v.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(v[i], i + 100);
    }
}

TEST(SmallVectorTest, MutableIndexingWritesThrough) {
    SmallVector<int, 2> v;
    for (int i = 0; i < 6; ++i) v.push_back(0);

    for (int i = 0; i < 6; ++i) v[i] = i * 7;
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(v[i], i * 7);
    }
}

TEST(SmallVectorTest, MoveConstructionTransfersOwnership) {
    SmallVector<int, 2> a;
    for (int i = 0; i < 8; ++i) a.push_back(i);

    SmallVector<int, 2> b(std::move(a));
    EXPECT_EQ(b.size(), 8u);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(b[i], i);
    }
    EXPECT_EQ(a.size(), 0u);
}

TEST(SmallVectorTest, MoveAssignmentReplacesContents) {
    SmallVector<int, 2> a;
    for (int i = 0; i < 8; ++i) a.push_back(i);

    SmallVector<int, 2> b;
    for (int i = 0; i < 3; ++i) b.push_back(99);

    b = std::move(a);
    EXPECT_EQ(b.size(), 8u);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(b[i], i);
    }
}

}  // namespace skwr
