#include "Game/Editor/AutoTile.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Game::Editor;
namespace LevelNs = NS::Game::Level;

TEST(AutoTileTest, IsolatedBlockHasZeroMask)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    auto m = EditorNs::ComputeNeighborMask(lv, 0, 0, 0, 1);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, AllSixNeighborsSetAllBits)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(-1, 0, 0, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, +1, 0, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, -1, 0, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, +1, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, -1, 1, 0));
    auto m = EditorNs::ComputeNeighborMask(lv, 0, 0, 0, 1);
    EXPECT_EQ(m, 0b00111111u);
}

TEST(AutoTileTest, DifferentBlockIdNeighborDoesNotSetBit)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, 2, 0)); // 違う blockId
    auto m = EditorNs::ComputeNeighborMask(lv, 0, 0, 0, 1);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, OnlyPlusXNeighborSetsBit0)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    lv.objects.push_back(LevelNs::MakeGridObject(+1, 0, 0, 1, 0));
    auto m = EditorNs::ComputeNeighborMask(lv, 0, 0, 0, 1);
    EXPECT_EQ(m, 0b00000001u);
}

TEST(AutoTileTest, SetSpawnMarkerWritesCoordinates)
{
    LevelNs::LevelData lv;
    EditorNs::SetSpawnMarker(lv, 10, 2, -5);
    EXPECT_EQ(lv.spawnX, 10);
    EXPECT_EQ(lv.spawnY, 2);
    EXPECT_EQ(lv.spawnZ, -5);
}

TEST(AutoTileTest, SetSpawnMarkerOverwritesPreviousValue)
{
    LevelNs::LevelData lv;
    EditorNs::SetSpawnMarker(lv, 1, 1, 1);
    EditorNs::SetSpawnMarker(lv, -3, 0, 7);
    EXPECT_EQ(lv.spawnX, -3);
    EXPECT_EQ(lv.spawnY, 0);
    EXPECT_EQ(lv.spawnZ, 7);
}
