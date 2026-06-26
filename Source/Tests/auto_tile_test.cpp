#include "Game/Blocks/AutoTile.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace BlocksNs = NS::Game::Blocks;

TEST(AutoTileTest, IsolatedBlockHasZeroMask)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, BlocksNs::kBlockIdSolid, 0));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, AllSixNeighborsSetAllBits)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(-1, 0, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, +1, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, -1, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, +1, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, -1, BlocksNs::kBlockIdSolid, 0));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00111111u);
}

TEST(AutoTileTest, OnlyPlusXNeighborSetsBit0)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, BlocksNs::kBlockIdSolid, 0));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00000001u);
}

TEST(AutoTileTest, SameVisualIdentityNeighborConnects)
{
    // 同じメッシュ参照 + マテリアルの隣接は kind を見ずに連結する。 同角度 slope 同士で確認する
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, BlocksNs::kBlockIdSlope45, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, BlocksNs::kBlockIdSlope45, 0));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00000001u);
}

TEST(AutoTileTest, DifferentMaterialNeighborDoesNotConnect)
{
    // メッシュは同じでもマテリアル添字が違えば視覚が異なるので連結しない
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, BlocksNs::kBlockIdSolid, 0));
    LevelNs::ObjectInstance neighbor = LevelNs::MakeGridObject(+1, 0, 0, BlocksNs::kBlockIdSolid, 0);
    neighbor.materialIndex = 7;
    lv.objects.push_back(neighbor);
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, DifferentVisualIdentityNeighborDoesNotSetBit)
{
    // solid (cube) の隣に slope (wedge) を置くとメッシュが異なるので連結しない
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, BlocksNs::kBlockIdSlope45, 0));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, MixedNeighborsConnectOnlySameVisual)
{
    // 視覚が一致する隣だけ bit が立つ。 別メッシュ (slope) や別マテリアルの solid は連結しない
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, BlocksNs::kBlockIdSolid, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, BlocksNs::kBlockIdSolid, 0));   // 同一視覚 → bit0
    lv.objects.push_back(LevelNs::MakeGridObject(-1, 0, 0, BlocksNs::kBlockIdSlope45, 0)); // 別メッシュ → 立たない
    LevelNs::ObjectInstance tinted = LevelNs::MakeGridObject(0, +1, 0, BlocksNs::kBlockIdSolid, 0);
    tinted.materialIndex = 3; // 別マテリアル → 立たない
    lv.objects.push_back(tinted);
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, +1, BlocksNs::kBlockIdSolid, 0)); // 同一視覚 → bit4
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00010001u);
}

TEST(AutoTileTest, SetSpawnMarkerWritesCoordinates)
{
    LevelNs::LevelData lv;
    BlocksNs::SetSpawnMarker(lv, 10, 2, -5);
    EXPECT_EQ(lv.spawnX, 10);
    EXPECT_EQ(lv.spawnY, 2);
    EXPECT_EQ(lv.spawnZ, -5);
}

TEST(AutoTileTest, SetSpawnMarkerOverwritesPreviousValue)
{
    LevelNs::LevelData lv;
    BlocksNs::SetSpawnMarker(lv, 1, 1, 1);
    BlocksNs::SetSpawnMarker(lv, -3, 0, 7);
    EXPECT_EQ(lv.spawnX, -3);
    EXPECT_EQ(lv.spawnY, 0);
    EXPECT_EQ(lv.spawnZ, 7);
}
