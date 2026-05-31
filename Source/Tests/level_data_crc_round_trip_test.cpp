#include "Game/Editor/BlockRegistry.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace EditorNs = NS::Game::Editor;

/// Play 中の LevelData 書込禁止保証。 600 tick (10 秒 @60Hz) を回した後の
/// CRC32 が Enter 前と一致することで、 PlayMode 経路で LevelData が変更されないことを
/// runtime にも検証する (compile-time の const& 受取と二段防御)。
TEST(PlayModeCrc, RoundTripPreservesLevelData_PMODE_06)
{
    LevelNs::LevelData level;
    level.spawnX = 5;
    level.spawnY = 1;
    level.spawnZ = -3;
    level.themeId = 7;
    level.coinThreshold = 30;
    level.timeLimitSeconds = 240;
    level.blocks.push_back({0, 0, 0, EditorNs::kBlockIdSolid, 0, 0});
    level.blocks.push_back({1, 0, 0, EditorNs::kBlockIdSolid, 1, 0});
    level.blocks.push_back({0, 0, 1, EditorNs::kBlockIdCoin, 0, 0});
    level.blocks.push_back({2, 0, 0, EditorNs::kBlockIdPowerStar, 0, 0});

    const std::uint32_t before = level.ComputeCrc32();

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(level, play);
    for (int i = 0; i < 600; ++i)
        mode.Tick(level, play, 1.0f / 60.0f);
    mode.Exit(play);

    EXPECT_EQ(level.ComputeCrc32(), before) << "PlayMode が LevelData を変更";
}

TEST(PlayModeCrc, RoundTripWithCoinCollectionPreservesLevelData)
{
    LevelNs::LevelData level;
    level.spawnX = 0;
    level.spawnY = 0;
    level.spawnZ = 0;
    level.blocks.push_back({0, 0, 0, EditorNs::kBlockIdCoin, 0, 0});
    const std::uint32_t before = level.ComputeCrc32();

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(level, play);
    for (int i = 0; i < 60; ++i)
        mode.Tick(level, play, 1.0f / 60.0f);
    EXPECT_GE(play.coinCount, 1);
    mode.Exit(play);

    EXPECT_EQ(level.ComputeCrc32(), before) << "Coin 取得時に LevelData 変更";
    EXPECT_EQ(level.blocks.size(), 1u);
    EXPECT_EQ(level.blocks[0].blockId, EditorNs::kBlockIdCoin);
}

TEST(PlayModeCrc, RoundTripWithStarContactPreservesLevelData)
{
    LevelNs::LevelData level;
    level.spawnX = 0;
    level.spawnY = 0;
    level.spawnZ = 0;
    level.blocks.push_back({0, 0, 0, EditorNs::kBlockIdPowerStar, 0, 0});
    const std::uint32_t before = level.ComputeCrc32();

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(level, play);
    for (int i = 0; i < 60; ++i)
        mode.Tick(level, play, 1.0f / 60.0f);
    EXPECT_TRUE(play.clearTriggered);
    mode.Exit(play);

    EXPECT_EQ(level.ComputeCrc32(), before) << "Star 接触時に LevelData 変更";
}
