#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;

TEST(LevelDataCrcTest, EmptyLevelIsDeterministic)
{
    LevelNs::LevelData a, b;
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, DifferentBlockIdsProduceDifferentCrc)
{
    LevelNs::LevelData a, b;
    a.blocks.push_back({1, 2, 3, 100, 0, 0});
    b.blocks.push_back({1, 2, 3, 101, 0, 0});
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, PlayStateMutationDoesNotAffectLevelDataCrc)
{
    LevelNs::LevelData level;
    level.blocks.push_back({0, 0, 0, 1, 0, 0});
    level.spawnX = 5;
    const auto before = level.ComputeCrc32();

    LevelNs::PlayState play;
    for (int i = 0; i < 100; ++i)
    {
        play.playerPosition.x += 0.1f;
        play.coinCount += 1;
        play.paused = !play.paused;
    }
    const auto after = level.ComputeCrc32();
    EXPECT_EQ(before, after);
}

TEST(LevelDataCrcTest, BlocksSizeIsHashed)
{
    LevelNs::LevelData a, b;
    a.blocks.push_back({0, 0, 0, 1, 0, 0});
    a.blocks.push_back({1, 0, 0, 1, 0, 0});
    b.blocks.push_back({0, 0, 0, 1, 0, 0});
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, ReservedFieldIsHashed)
{
    LevelNs::LevelData a, b;
    a.blocks.push_back({0, 0, 0, 1, 0, 0});
    b.blocks.push_back({0, 0, 0, 1, 0, 1});
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, MetadataFieldsAreHashed)
{
    LevelNs::LevelData a, b;
    a.themeId = 1;
    b.themeId = 2;
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, VectorCapacityDoesNotAffectCrc)
{
    LevelNs::LevelData a, b;
    a.blocks.push_back({0, 0, 0, 1, 0, 0});
    b.blocks.reserve(1000);
    b.blocks.push_back({0, 0, 0, 1, 0, 0});
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}
