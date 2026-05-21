#include <gtest/gtest.h>

#include <Game/Block.h>

TEST(BlockTest, ConstructsWithGivenHalfExtents)
{
    Block block(nullptr, nullptr, {1.0f, 0.5f, 2.0f});
    EXPECT_EQ(block.Components().size(), 2u);
    auto half = block.Collider().HalfExtents();
    EXPECT_FLOAT_EQ(half.x, 1.0f);
    EXPECT_FLOAT_EQ(half.y, 0.5f);
    EXPECT_FLOAT_EQ(half.z, 2.0f);
}

TEST(BlockTest, WorldAABBReflectsPositionAndHalfExtents)
{
    Block block(nullptr, nullptr, {1.0f, 0.5f, 2.0f});
    block.Root().SetPosition({3.0f, 4.0f, -1.0f});
    auto aabb = block.Collider().WorldAABB();
    EXPECT_FLOAT_EQ(aabb.Center.x, 3.0f);
    EXPECT_FLOAT_EQ(aabb.Center.y, 4.0f);
    EXPECT_FLOAT_EQ(aabb.Center.z, -1.0f);
    EXPECT_FLOAT_EQ(aabb.Extents.x, 1.0f);
    EXPECT_FLOAT_EQ(aabb.Extents.y, 0.5f);
    EXPECT_FLOAT_EQ(aabb.Extents.z, 2.0f);
}

TEST(BlockTest, AccessorsReturnInternalReferences)
{
    Block block(nullptr, nullptr, {0.5f, 0.5f, 0.5f});
    EXPECT_EQ(&block.MeshComp(), block.Components()[0]);
    EXPECT_EQ(&block.Collider(), block.Components()[1]);
}
