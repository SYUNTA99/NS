#include "Game/Editor/BlockRegistry.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace EditorNs = NS::Game::Editor;

TEST(PlayMode, EnterInitializesPlayerAtSpawn)
{
    LevelNs::LevelData lv;
    lv.spawnX = 5;
    lv.spawnY = 2;
    lv.spawnZ = 3;
    LevelNs::PlayState play;
    LevelNs::PlayMode mode;

    mode.Enter(lv, play);

    EXPECT_NEAR(play.playerPosition.x, 5.0f, 1e-4f);
    // y は spawn セル底面 + (capsule halfHeight + radius) + 1cm lift = spawnY + 0.41。
    EXPECT_NEAR(play.playerPosition.y, 2.41f, 1e-3f);
    EXPECT_NEAR(play.playerPosition.z, 3.0f, 1e-4f);
    EXPECT_EQ(play.coinCount, 0);
    EXPECT_FALSE(play.paused);
    EXPECT_FALSE(play.deathTriggered);
    EXPECT_FALSE(play.clearTriggered);
}

TEST(PlayMode, PausedTickDoesNotChangeVelocity)
{
    LevelNs::LevelData lv;
    LevelNs::PlayState play;
    LevelNs::PlayMode mode;

    mode.Enter(lv, play);
    play.paused = true;
    const auto v0 = play.playerVelocity;
    for (int i = 0; i < 60; ++i)
        mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(play.playerVelocity.x, v0.x);
    EXPECT_FLOAT_EQ(play.playerVelocity.y, v0.y);
    EXPECT_FLOAT_EQ(play.playerVelocity.z, v0.z);
}

TEST(PlayMode, FallDeathTriggersAtThreshold)
{
    LevelNs::LevelData lv;
    LevelNs::PlayState play;
    LevelNs::PlayMode mode;

    mode.Enter(lv, play);
    for (int i = 0; i < 500; ++i)
        mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_TRUE(play.deathTriggered);
}

TEST(PlayMode, CoinContactIncrementsCounter)
{
    LevelNs::LevelData lv;
    lv.spawnX = 0;
    lv.spawnY = 0;
    lv.spawnZ = 0;
    // player の spawn セル中心と同じ位置に coin を置くと中心距離 0 で必ず pickup。
    lv.blocks.push_back({0, 0, 0, EditorNs::kBlockIdCoin, 0, 0});

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_GE(play.coinCount, 1);

    const auto c1 = play.coinCount;
    mode.Tick(lv, play, 1.0f / 60.0f);
    EXPECT_EQ(play.coinCount, c1);
}

TEST(PlayMode, PowerStarTriggersClear)
{
    LevelNs::LevelData lv;
    lv.spawnX = 0;
    lv.spawnY = 0;
    lv.spawnZ = 0;
    lv.blocks.push_back({0, 0, 0, EditorNs::kBlockIdPowerStar, 0, 0});

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_TRUE(play.clearTriggered);
}

TEST(PlayMode, ExitResetsTransientFlags)
{
    LevelNs::LevelData lv;
    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    play.paused = true;
    play.clearTriggered = true;
    play.deathTriggered = true;

    mode.Exit(play);

    EXPECT_FALSE(play.paused);
    EXPECT_FALSE(play.clearTriggered);
    EXPECT_FALSE(play.deathTriggered);
}
