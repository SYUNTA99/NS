#include <gtest/gtest.h>

#include <Framework/Scene/Components/HazardComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Game/Level/LevelData.h>
#include <Game/Level/PlayState.h>

namespace
{
    using NS::Scene::GameObject;
    using NS::Scene::HazardComponent;
} // namespace

TEST(HazardDamageTest, PlayStateInitializesHealthTo8)
{
    NS::Game::Level::PlayState play;
    EXPECT_EQ(play.playerHealth, 8);
}

TEST(HazardDamageTest, HazardDecrementsHealthOnOverlap)
{
    GameObject hazardObj;
    auto& hazard = *hazardObj.AddComponent<HazardComponent>();
    NS::Game::Level::PlayState play;

    hazard.OnPlayerOverlap(play);

    EXPECT_EQ(play.playerHealth, 7);
    EXPECT_FALSE(play.deathTriggered);
}

TEST(HazardDamageTest, HealthClampsAtZero)
{
    GameObject hazardObj;
    auto& hazard = *hazardObj.AddComponent<HazardComponent>();
    NS::Game::Level::PlayState play;

    for (int i = 0; i < 10; ++i)
        hazard.OnPlayerOverlap(play);

    EXPECT_EQ(play.playerHealth, 0);
    EXPECT_GE(play.playerHealth, 0);
}

TEST(HazardDamageTest, HazardDoesNotModifyLevelData)
{
    NS::Game::Level::LevelData level;
    level.objects.push_back(NS::Game::Level::MakeGridObject(0, 0, 0, 220, 0));
    level.spawnX = 1;
    level.spawnY = 2;
    level.spawnZ = 3;
    level.themeId = 4;
    const std::uint32_t crcBefore = level.ComputeCrc32();

    GameObject hazardObj;
    auto& hazard = *hazardObj.AddComponent<HazardComponent>();
    NS::Game::Level::PlayState play;

    for (int i = 0; i < 5; ++i)
        hazard.OnPlayerOverlap(play);

    const std::uint32_t crcAfter = level.ComputeCrc32();
    EXPECT_EQ(crcBefore, crcAfter);
    EXPECT_LT(play.playerHealth, 8);
}

TEST(HazardDamageTest, HealthZeroFlagsDeath)
{
    GameObject hazardObj;
    auto& hazard = *hazardObj.AddComponent<HazardComponent>();
    NS::Game::Level::PlayState play;

    for (int i = 0; i < 8; ++i)
        hazard.OnPlayerOverlap(play);

    EXPECT_EQ(play.playerHealth, 0);
    EXPECT_TRUE(play.deathTriggered);
}
