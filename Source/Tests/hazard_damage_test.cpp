#include <gtest/gtest.h>

#include <Game/Level/LevelData.h>
#include <Game/Level/PlayMode.h>
#include <Game/Level/PlayState.h>

TEST(HazardDamageTest, PlayStateInitializesHealthTo8)
{
    NS::Game::Level::PlayState play;
    EXPECT_EQ(play.playerHealth, 8);
}

TEST(HazardDamageTest, ContactDamageDecrementsHealth)
{
    NS::Game::Level::PlayState play;

    NS::Game::Level::ApplyContactDamage(play);

    EXPECT_EQ(play.playerHealth, 7);
    EXPECT_FALSE(play.deathTriggered);
}

TEST(HazardDamageTest, HealthClampsAtZero)
{
    NS::Game::Level::PlayState play;

    for (int i = 0; i < 10; ++i)
        NS::Game::Level::ApplyContactDamage(play);

    EXPECT_EQ(play.playerHealth, 0);
    EXPECT_GE(play.playerHealth, 0);
}

TEST(HazardDamageTest, ContactDamageDoesNotModifyLevelData)
{
    NS::Game::Level::LevelData level;
    level.objects.push_back(NS::Game::Level::MakeGridObject(0, 0, 0, 0));
    level.objects.push_back(
        NS::Game::Level::MakePlayerObject(NS::Math::Vector3{1.0f, 2.0f, 3.0f}, NS::Math::Quaternion{}));
    level.environment.blockTextureBaseSlice = 8;
    const std::uint32_t crcBefore = level.ComputeCrc32();

    NS::Game::Level::PlayState play;
    for (int i = 0; i < 5; ++i)
        NS::Game::Level::ApplyContactDamage(play);

    const std::uint32_t crcAfter = level.ComputeCrc32();
    EXPECT_EQ(crcBefore, crcAfter);
    EXPECT_LT(play.playerHealth, 8);
}

TEST(HazardDamageTest, HealthZeroFlagsDeath)
{
    NS::Game::Level::PlayState play;

    for (int i = 0; i < 8; ++i)
        NS::Game::Level::ApplyContactDamage(play);

    EXPECT_EQ(play.playerHealth, 0);
    EXPECT_TRUE(play.deathTriggered);
}
