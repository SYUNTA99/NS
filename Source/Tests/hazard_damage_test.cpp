#include <gtest/gtest.h>

#include <GameCore/Level/LevelData.h>
#include <GameCore/Level/PlayMode.h>
#include <GameCore/Level/PlayState.h>

TEST(HazardDamageTest, PlayStateInitializesHealthTo8)
{
    NS::GameCore::Level::PlayState play;
    EXPECT_EQ(play.playerHealth, 8);
}

TEST(HazardDamageTest, ContactDamageDecrementsHealth)
{
    NS::GameCore::Level::PlayState play;

    NS::GameCore::Level::ApplyContactDamage(play);

    EXPECT_EQ(play.playerHealth, 7);
    EXPECT_FALSE(play.deathTriggered);
}

TEST(HazardDamageTest, HealthClampsAtZero)
{
    NS::GameCore::Level::PlayState play;

    for (int i = 0; i < 10; ++i)
        NS::GameCore::Level::ApplyContactDamage(play);

    EXPECT_EQ(play.playerHealth, 0);
    EXPECT_GE(play.playerHealth, 0);
}

TEST(HazardDamageTest, ContactDamageDoesNotModifyLevelData)
{
    NS::GameCore::Level::LevelData level;
    level.objects.push_back(NS::GameCore::Level::MakeCellObject(0, 0, 0, 0));
    level.objects.push_back(
        NS::GameCore::Level::MakePlayerObject(NS::Math::Vector3{1.0f, 2.0f, 3.0f}, NS::Math::Quaternion{}));
    const std::uint32_t crcBefore = level.ComputeCrc32();

    NS::GameCore::Level::PlayState play;
    for (int i = 0; i < 5; ++i)
        NS::GameCore::Level::ApplyContactDamage(play);

    const std::uint32_t crcAfter = level.ComputeCrc32();
    EXPECT_EQ(crcBefore, crcAfter);
    EXPECT_LT(play.playerHealth, 8);
}

TEST(HazardDamageTest, HealthZeroFlagsDeath)
{
    NS::GameCore::Level::PlayState play;

    for (int i = 0; i < 8; ++i)
        NS::GameCore::Level::ApplyContactDamage(play);

    EXPECT_EQ(play.playerHealth, 0);
    EXPECT_TRUE(play.deathTriggered);
}
