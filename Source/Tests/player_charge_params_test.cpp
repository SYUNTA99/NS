#include "Editor/EditorActors.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
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
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    GL::Player::PlayerParams* params = &player.Params();
    const nlohmann::json fields = NS::SerializeClassObjFields(params);
    const nlohmann::json expected = {
        {"チャージしきい値秒", 0.2f}, {"チャージ満タン秒", 1.0f}, {"チャージ減速率", 0.7f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
    // 構えの縮みは体の形を描く PlayerAppearance の欄
    const nlohmann::json appearance = NS::SerializeClassObjFields(&player.Appearance());
    const nlohmann::json squash = {{"構えの縮み", 0.95f}, {"押しの構えの縮み", 0.97f}};
    for (nlohmann::json::const_iterator it = squash.begin(); it != squash.end(); ++it)
    {
        ASSERT_TRUE(appearance.contains(it.key())) << it.key();
        EXPECT_EQ(appearance[it.key()], it.value()) << it.key();
        EXPECT_FALSE(fields.contains(it.key())) << it.key();
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
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    GL::Player::PlayerParams* params = &player.Params();
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(0.5f), 1.5f);

    ASSERT_EQ(
        NS::ApplyJsonFields(
            params, {{"チャージ減速率", 0.4f}, {"チャージ倍率カーブ", {{"curve", {{0.0f, 1.0f}, {1.0f, 3.0f}}}}}}),
        0u);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(0.5f), 2.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(2.0f), 3.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(std::numeric_limits<float>::quiet_NaN()), 1.0f);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(std::numeric_limits<float>::infinity()), 1.0f);
    EXPECT_NEAR(params->ChargingSpeedScale(), 0.6f, 0.00001f);

    // 減速率は 0..1 の外でも倍率が 0..1 に収まる
    ASSERT_EQ(NS::ApplyJsonFields(params, {{"チャージ減速率", 1.5f}}), 0u);
    EXPECT_FLOAT_EQ(params->ChargingSpeedScale(), 0.0f);
    ASSERT_EQ(NS::ApplyJsonFields(params, {{"チャージ減速率", -0.5f}}), 0u);
    EXPECT_FLOAT_EQ(params->ChargingSpeedScale(), 1.0f);

    // Inspector で点を全部消すとカーブは 0 を返す。威力が消えないよう 1 とみなす
    ASSERT_EQ(NS::ApplyJsonFields(params, {{"チャージ倍率カーブ", {{"curve", nlohmann::json::array()}}}}), 0u);
    EXPECT_FLOAT_EQ(params->ChargeFactorFor(0.5f), 1.0f);
}

TEST(PlayerChargeParams, LiveParamsDriveTheJudgeCurves)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    GL::Player::PlayerParams* params = &player.Params();
    ASSERT_EQ(NS::ApplyJsonFields(params,
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

// .actor が持つ形 (欄を書き出した JSON) を別の自機へ読むと、カーブも同じ値になる
TEST(PlayerChargeParams, CurvesSurviveWritingAndReadingTheFields)
{
    const std::unique_ptr<PlayerActor> written = NS::CreateActor<PlayerActor>();
    ASSERT_EQ(NS::ApplyJsonFields(
                  &written->Params(),
                  {{"チャージ減速率", 0.25f}, {"チャージ倍率カーブ", {{"curve", {{0.0f, 1.0f}, {1.0f, 3.0f}}}}}}),
              0u);
    const nlohmann::json fields = NS::SerializeClassObjFields(&written->Params());

    const std::unique_ptr<PlayerActor> read = NS::CreateActor<PlayerActor>();
    ASSERT_EQ(NS::ApplyJsonFields(&read->Params(), fields), 0u);
    EXPECT_FLOAT_EQ(read->Params().ChargingSpeedScale(), 0.75f);
    EXPECT_FLOAT_EQ(read->Params().ChargeFactorFor(0.5f), 2.0f);
}

// 消した欄の鍵が同梱の .actor と場面に残ると、読むたびに知らない鍵として捨てられる
TEST(PlayerChargeParams, ShippedAssetsCarryNoRemovedTierKeys)
{
    const std::vector<std::string_view> removed{"突進位置係数カーブ", "中心近くの境目"};
    const nlohmann::json* actorFile = NS::ActorFileLibrary::Get().Find("PlayerActor");
    ASSERT_NE(actorFile, nullptr);
    const nlohmann::json* actorFileFields = NS::ClassObjFields(*actorFile, "Params");
    ASSERT_NE(actorFileFields, nullptr);
    for (const std::string_view key : removed)
    {
        EXPECT_FALSE(NS::HasField(*actorFileFields, key)) << key;
    }

    for (const std::string_view sceneName : {"new_scene.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"),
                                        "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, path)) << sceneName;
        for (const nlohmann::json& entry : NS::SceneJsonActors(doc))
        {
            const nlohmann::json* fields = NS::ClassObjFields(entry, "Params");
            if (NS::ActorJsonFile(entry) != "PlayerActor" || fields == nullptr)
            {
                continue;
            }
            for (const std::string_view key : removed)
            {
                EXPECT_FALSE(NS::HasField(*fields, key)) << sceneName << " " << key;
            }
        }
    }
}

// クラスに無い部品の鍵が同梱の .actor と場面に残ると、読むたびに読み飛ばしの警告が出る
TEST(PlayerChargeParams, ShippedAssetsNameOnlyPartsTheClassHas)
{
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    const nlohmann::json prototype = NS::Editor::MakePrototypeJson(playerOwned.get());
    const nlohmann::json* actorFile = NS::ActorFileLibrary::Get().Find("PlayerActor");
    ASSERT_NE(actorFile, nullptr);
    for (nlohmann::json::const_iterator it = NS::ActorJsonClassObjs(*actorFile).begin();
         it != NS::ActorJsonClassObjs(*actorFile).end();
         ++it)
    {
        EXPECT_NE(NS::ClassObjFields(prototype, it.key()), nullptr) << "PlayerActor.actor " << it.key();
    }

    for (const std::string_view sceneName : {"new_scene.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"),
                                        "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, path)) << sceneName;
        for (const nlohmann::json& entry : NS::SceneJsonActors(doc))
        {
            if (NS::ActorJsonFile(entry) != "PlayerActor")
            {
                continue;
            }
            const nlohmann::json& parts = NS::ActorJsonClassObjs(entry);
            for (nlohmann::json::const_iterator it = parts.begin(); it != parts.end(); ++it)
            {
                EXPECT_NE(NS::ClassObjFields(prototype, it.key()), nullptr) << sceneName << " " << it.key();
            }
        }
    }
}
