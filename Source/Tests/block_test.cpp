#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Game/Block.h>

#include <cmath>

namespace
{
    bool BlockMatricesNear(const NS::Math::Matrix& a, const NS::Math::Matrix& b, float eps = 1e-4f)
    {
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                if (std::abs(a.m[row][col] - b.m[row][col]) > eps)
                    return false;
        return true;
    }
} // namespace

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

TEST(BlockTest, PlacedBlockWithoutSnapshotSwingsTowardOrigin)
{
    // 編集再構築の置き直しと同じ手順。Snapshot を呼ばないと previous が default のまま残り
    // 描画 alpha<1 で配置先ではなく原点へ補間される(振れバグの再現)
    Block block(nullptr, nullptr, {0.5f, 0.5f, 0.5f});
    block.Root().SetPosition({5.0f, 3.0f, -2.0f});
    block.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(1.0f, 0.0f, 0.0f));

    EXPECT_FALSE(BlockMatricesNear(block.Root().InterpolatedWorldMatrix(0.5f), block.Root().WorldMatrix()))
        << "Snapshot 前は中間 alpha が配置先と一致せず原点へ振れる";
}

TEST(BlockTest, PlacedBlockWithSnapshotStaysStillAcrossAlpha)
{
    // RebuildBlocksFromLevelData の修正後と同じ手順。配置直後に Snapshot すれば previous==current となり
    // 全 alpha で WorldMatrix と一致し、編集のたびの振れが消える
    Block block(nullptr, nullptr, {0.5f, 0.5f, 0.5f});
    block.Root().SetPosition({5.0f, 3.0f, -2.0f});
    block.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(1.0f, 0.0f, 0.0f));
    block.Root().Snapshot();

    const NS::Math::Matrix world = block.Root().WorldMatrix();
    EXPECT_TRUE(BlockMatricesNear(block.Root().InterpolatedWorldMatrix(0.0f), world));
    EXPECT_TRUE(BlockMatricesNear(block.Root().InterpolatedWorldMatrix(0.5f), world));
    EXPECT_TRUE(BlockMatricesNear(block.Root().InterpolatedWorldMatrix(1.0f), world));
}
