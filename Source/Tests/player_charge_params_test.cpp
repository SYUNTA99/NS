#include "Game/Player.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/SubObjectEntry.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/FileSystem.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

TEST(PlayerChargeParams, DefaultsKeepEveryFieldAndCurve)
{
    Player player;
    player.Init();
    NS::Game::Player::PlayerParams* params =
        NS::Obj::Cast<NS::Game::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    const nlohmann::json fields = NS::Obj::SerializeSubObjectFields(*params);
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
}

// 溜めの倍率と溜め中の減速は欄の持ち主 PlayerParams が答える。裁定役と自機の溜めは同じ答えを読む
TEST(PlayerChargeParams, ChargeFactorComesFromTheParamsCurve)
{
    Player player;
    player.Init();
    NS::Game::Player::PlayerParams* params =
        NS::Obj::Cast<NS::Game::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(0.5f), 1.5f);

    ASSERT_EQ(
        NS::Obj::ApplyJsonFields(
            *params, {{"チャージ減速率", 0.4f}, {"チャージ倍率カーブ", {{"curve", {{0.0f, 1.0f}, {1.0f, 3.0f}}}}}}),
        0u);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(0.5f), 2.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(2.0f), 3.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(std::numeric_limits<float>::quiet_NaN()), 1.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(std::numeric_limits<float>::infinity()), 1.0f);
    EXPECT_NEAR(params->ChargingSpeedScale(), 0.6f, 0.00001f);

    // 減速率は 0..1 の外でも倍率が 0..1 に収まる
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"チャージ減速率", 1.5f}}), 0u);
    EXPECT_FLOAT_EQ(params->ChargingSpeedScale(), 0.0f);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"チャージ減速率", -0.5f}}), 0u);
    EXPECT_FLOAT_EQ(params->ChargingSpeedScale(), 1.0f);

    // Inspector で点を全部消すとカーブは 0 を返す。威力が消えないよう 1 とみなす
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"チャージ倍率カーブ", {{"curve", nlohmann::json::array()}}}}), 0u);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(0.5f), 1.0f);
}

TEST(PlayerChargeParams, LiveParamsDriveTheJudgeCurves)
{
    Player player;
    player.Init();
    NS::Game::Player::PlayerParams* params =
        NS::Obj::Cast<NS::Game::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params,
                                       {{"チャージ減速率", 0.4f},
                                        {"チャージしきい値秒", 0.1f},
                                        {"チャージ満タン秒", 0.5f},
                                        {"チャージ倍率カーブ", {{"curve", {{0.0f, 2.0f}, {1.0f, 4.0f}}}}}}),
              0u);
    player.Update(false);
    EXPECT_EQ(player.ChargeJudge().chargeThresholdSteps,
              static_cast<int>(std::lround(0.1f / NS::OS::FrameTimer::FixedDelta())));
    EXPECT_EQ(player.ChargeJudge().chargeMaxSteps,
              static_cast<int>(std::lround(0.5f / NS::OS::FrameTimer::FixedDelta())));
}

TEST(PlayerChargeParams, SceneOverridesKeepTheirCurvesAfterReload)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    entry["subObjects"] = {
        {"Params", {{"チャージ減速率", 0.25f}, {"チャージ倍率カーブ", {{"curve", {{0.0f, 1.0f}, {1.0f, 3.0f}}}}}}}};
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(doc);
    scene.LoadJson(scene.ToJson());
    const Player* player = static_cast<const Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    EXPECT_FLOAT_EQ(player->Params().ChargingSpeedScale(), 0.75f);
    EXPECT_FLOAT_EQ(player->Params().ChargeFactorFor(0.5f), 2.0f);
}

// 消した欄の鍵が同梱の種類の既定値と場面に残ると、読むたびに知らない鍵として捨てられる
TEST(PlayerChargeParams, ShippedAssetsCarryNoRemovedTierKeys)
{
    const std::vector<std::string_view> removed{"突進位置係数カーブ", "中心近くの境目"};
    const nlohmann::json* archetype = NS::Obj::ArchetypeLibrary::Get().Find("Player");
    ASSERT_NE(archetype, nullptr);
    const nlohmann::json* archetypeFields = NS::Obj::SubObjFields(*archetype, "Params");
    ASSERT_NE(archetypeFields, nullptr);
    for (const std::string_view key : removed)
    {
        EXPECT_FALSE(NS::Obj::HasField(*archetypeFields, key)) << key;
    }

    for (const std::string_view sceneName : {"new_scene.scene", "course.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"),
                                        "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::Obj::LoadSceneFromJsonFile(doc, path)) << sceneName;
        for (const nlohmann::json& entry : NS::Obj::SceneJsonObjects(doc))
        {
            const nlohmann::json* fields = NS::Obj::SubObjFields(entry, "Params");
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

// クラスに無い部品の鍵が同梱の種類の既定値と場面に残ると、読むたびに読み飛ばしの警告が出る
TEST(PlayerChargeParams, ShippedAssetsNameOnlyPartsTheClassHas)
{
    Player player;
    player.Init();
    const nlohmann::json* archetype = NS::Obj::ArchetypeLibrary::Get().Find("Player");
    ASSERT_NE(archetype, nullptr);
    for (nlohmann::json::const_iterator it = NS::Obj::ObjectJsonSubObjs(*archetype).begin();
         it != NS::Obj::ObjectJsonSubObjs(*archetype).end();
         ++it)
    {
        EXPECT_NE(player.FindSubObj(it.key()), nullptr) << "Player.json " << it.key();
    }

    for (const std::string_view sceneName : {"new_scene.scene", "course.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"),
                                        "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::Obj::LoadSceneFromJsonFile(doc, path)) << sceneName;
        for (const nlohmann::json& entry : NS::Obj::SceneJsonObjects(doc))
        {
            if (NS::Obj::ObjectJsonClass(entry) != "Player")
            {
                continue;
            }
            const nlohmann::json& parts = NS::Obj::ObjectJsonSubObjs(entry);
            for (nlohmann::json::const_iterator it = parts.begin(); it != parts.end(); ++it)
            {
                EXPECT_NE(player.FindSubObj(it.key()), nullptr) << sceneName << " " << it.key();
            }
        }
    }
}
