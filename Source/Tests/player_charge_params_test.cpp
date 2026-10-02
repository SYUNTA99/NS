#include "Game/Level/CollisionInput.h"
#include "Game/Player.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Reflection/Archetype.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Platform/FileSystem.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

TEST(PlayerChargeParams, DefaultsKeepEveryFieldAndCurve)
{
    Player player;
    NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player.Part("Params"));
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl"));
    ASSERT_NE(params, nullptr);
    ASSERT_NE(input, nullptr);
    const nlohmann::json fields = NS::Obj::SerializeComponent(*params)["fields"];
    const nlohmann::json expected = {{"チャージしきい値秒", 0.2f},
                                     {"チャージ満タン秒", 1.0f},
                                     {"チャージ減速率", 0.7f},
                                     {"構えの縮み", 0.95f},
                                     {"押しの構えの縮み", 0.97f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
    ASSERT_TRUE(fields.contains("チャージ倍率カーブ"));
    // 段と威力は相手の面の赤で決める。鍵が戻ると、保存した場面に効かない境目と曲線が載る
    EXPECT_FALSE(fields.contains("突進位置係数カーブ"));
    EXPECT_FALSE(fields.contains("中心近くの境目"));
    // 惜しいの段は消した。鍵が戻ると、保存した場面に段の無い境目が載る
    EXPECT_FALSE(fields.contains("惜しいの境目"));
    // 左右の寄せは消した。鍵が戻ると、保存した場面に効かない探す範囲が載る
    EXPECT_FALSE(fields.contains("寄せる相手を探す角度"));
    EXPECT_FALSE(fields.contains("寄せる相手を探す距離"));
    EXPECT_FLOAT_EQ(input->ChargeFactorFor(0.5f), 1.5f);
    EXPECT_TRUE(NS::Obj::SerializeComponent(*input)["fields"].empty());
}

TEST(PlayerChargeParams, LiveParamsDriveTheJudgeCurves)
{
    Player player;
    NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player.Part("Params"));
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl"));
    ASSERT_NE(params, nullptr);
    ASSERT_NE(input, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params,
                                       {{"チャージ減速率", 0.4f},
                                        {"チャージしきい値秒", 0.1f},
                                        {"チャージ満タン秒", 0.5f},
                                        {"チャージ倍率カーブ", {{"curve", {{0.0f, 2.0f}, {1.0f, 4.0f}}}}}}),
              0u);
    EXPECT_NEAR(input->ChargingSpeedScale(), 0.6f, 0.00001f);
    EXPECT_FLOAT_EQ(input->ChargeFactorFor(0.5f), 3.0f);
    input->OnStart();
    input->OnUpdate();
    EXPECT_EQ(input->Judge().chargeThresholdSteps,
              static_cast<int>(std::lround(0.1f / NS::Platform::FrameTimer::FixedDelta())));
    EXPECT_EQ(input->Judge().chargeMaxSteps,
              static_cast<int>(std::lround(0.5f / NS::Platform::FrameTimer::FixedDelta())));
}

TEST(PlayerChargeParams, SceneOverridesKeepTheirCurvesAfterReload)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    entry["parts"] = {
        {"Params", {{"チャージ減速率", 0.25f}, {"チャージ倍率カーブ", {{"curve", {{0.0f, 1.0f}, {1.0f, 3.0f}}}}}}}};
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(doc);
    scene.LoadJson(scene.ToJson());
    const Player* player = static_cast<const Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    const NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player->Part("ChargeControl"));
    ASSERT_NE(input, nullptr);
    EXPECT_FLOAT_EQ(input->ChargingSpeedScale(), 0.75f);
    EXPECT_FLOAT_EQ(input->ChargeFactorFor(0.5f), 2.0f);
}

// 消した欄の鍵が同梱の種類の既定値と場面に残ると、読むたびに知らない鍵として捨てられる
TEST(PlayerChargeParams, ShippedAssetsCarryNoRemovedTierKeys)
{
    const std::vector<std::string_view> removed{"突進位置係数カーブ", "中心近くの境目"};
    const nlohmann::json* archetype = NS::Obj::ArchetypeLibrary::Get().Find("Player");
    ASSERT_NE(archetype, nullptr);
    const nlohmann::json* archetypeFields = NS::Obj::PartFields(*archetype, "Params");
    ASSERT_NE(archetypeFields, nullptr);
    for (const std::string_view key : removed)
    {
        EXPECT_FALSE(NS::Obj::HasField(*archetypeFields, key)) << key;
    }

    for (const std::string_view sceneName : {"new_scene.scene", "course.scene"})
    {
        const std::string path = NS::Platform::FileSystem::Combine(
            NS::Platform::FileSystem::Combine(
                NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Assets"), "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::Obj::LoadSceneFromJsonFile(doc, path)) << sceneName;
        for (const nlohmann::json& entry : NS::Obj::SceneJsonObjects(doc))
        {
            const nlohmann::json* fields = NS::Obj::PartFields(entry, "Params");
            if (NS::Obj::ObjectJsonClass(entry) != "Player" || fields == nullptr)
            {
                continue;
            }
            for (const std::string_view key : removed)
            {
                EXPECT_FALSE(NS::Obj::HasField(*fields, key)) << sceneName << " " << key;
            }
        }
    }
}
