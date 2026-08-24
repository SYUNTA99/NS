#include <Game/Player/PlayerStatsManagerComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using NS::Game::Player::PlayerStatsManagerComponent;

    // 移し替え前とシーン JSON に載っている綴り。半角空白 1 つのずれでも値が読めなくなる
    const std::vector<std::string> k_LegacyFieldNames = {"ジャンプ初速",
                                                         "上昇重力",
                                                         "下降重力",
                                                         "頂点滞空 Vy",
                                                         "頂点滞空倍率",
                                                         "ジャンプ離し倍率",
                                                         "コヨーテ時間",
                                                         "先行入力時間",
                                                         "歩き速度",
                                                         "加速時定数",
                                                         "減速時定数",
                                                         "スティック遊び",
                                                         "突進速度",
                                                         "突進距離",
                                                         "タップ初速",
                                                         "タップの上向き初速",
                                                         "タップ距離"};

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

TEST(PlayerStatsManagerTest, ReflectedFieldNamesMatchLegacy)
{
    NS::Object::GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();

    const NS::Object::ReflectionInfo* info = stats.GetReflection();
    ASSERT_NE(info, nullptr);

    std::vector<std::string> actual;
    actual.reserve(info->fieldCount);
    for (std::size_t i = 0; i < info->fieldCount; ++i)
        actual.emplace_back(info->fields[i].name);
    std::sort(actual.begin(), actual.end());

    std::vector<std::string> expected = k_LegacyFieldNames;
    std::sort(expected.begin(), expected.end());

    EXPECT_EQ(actual, expected) << "欄の表示名がずれている。シーン JSON の値が既定へ化ける";
}

TEST(PlayerStatsManagerTest, NonFiniteWriteKeepsValue)
{
    NS::Object::GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();

    const float k_Rejected[] = {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()};

    for (const std::string& name : k_LegacyFieldNames)
    {
        const float original = ReadField(stats, name.c_str());
        ASSERT_TRUE(std::isfinite(original)) << name;

        for (float rejected : k_Rejected)
        {
            WriteField(stats, name.c_str(), rejected);
            EXPECT_FLOAT_EQ(ReadField(stats, name.c_str()), original) << name;
        }
    }
}
