#include <Game/Player/PlayerStatsManagerComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

namespace
{
    using NS::Game::Player::PlayerStatsManagerComponent;

    float ReadField(const PlayerStatsManagerComponent& stats, const char* name)
    {
        const NS::Object::FieldDesc* field = NS::Object::FindField(stats.GetReflection(), name);
        EXPECT_NE(field, nullptr) << name;
        if (field == nullptr)
            return std::numeric_limits<float>::quiet_NaN();

        float value = 0.0f;
        field->get(&stats, &value);
        return value;
    }

    void WriteField(PlayerStatsManagerComponent& stats, const char* name, float value)
    {
        const NS::Object::FieldDesc* field = NS::Object::FindField(stats.GetReflection(), name);
        ASSERT_NE(field, nullptr) << name;
        field->set(&stats, &value);
    }
} // namespace

TEST(PlayerStatsManagerTest, StartsWithASingleSet)
{
    NS::Object::GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();

    EXPECT_EQ(stats.StatsCount(), 1u);
    EXPECT_EQ(stats.CurrentIndex(), 0u);
}

TEST(PlayerStatsManagerTest, ReflectsSeventeenFields)
{
    NS::Object::GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();

    const NS::Object::ReflectionInfo* info = stats.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 17u);
}

TEST(PlayerStatsManagerTest, ReadsTheJumpImpulseThroughReflection)
{
    NS::Object::GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();

    EXPECT_FLOAT_EQ(ReadField(stats, "ジャンプ初速"), 12.0f);
}

TEST(PlayerStatsManagerTest, WriteThroughReflectionReachesTheCurrentSet)
{
    NS::Object::GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();

    WriteField(stats, "先行入力時間", 0.4f);

    EXPECT_FLOAT_EQ(stats.Current().jumpBufferTime, 0.4f);
}

TEST(PlayerStatsManagerTest, SetterDropsNonFiniteValues)
{
    NS::Object::GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();

    stats.SetJumpImpulse(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(stats.Current().jumpImpulse, 12.0f);

    stats.SetJumpImpulse(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(stats.Current().jumpImpulse, 12.0f);

    stats.SetJumpImpulse(-std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(stats.Current().jumpImpulse, 12.0f);
}
