#include "Game/Level/CollisionInput.h"
#include "Game/Player.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"

#include <gtest/gtest.h>

#include <cmath>

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
                                     {"中心近くの境目", 0.35f},
                                     {"寄せる相手を探す角度", 30.0f},
                                     {"寄せる相手を探す距離", 6.0f},
                                     {"構えの縮み", 0.95f},
                                     {"押しの構えの縮み", 0.97f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
    ASSERT_TRUE(fields.contains("チャージ倍率カーブ"));
    ASSERT_TRUE(fields.contains("突進位置係数カーブ"));
    // 惜しいの段は消した。鍵が戻ると、保存した場面に段の無い境目が載る
    EXPECT_FALSE(fields.contains("惜しいの境目"));
    EXPECT_FLOAT_EQ(input->ChargeFactorFor(0.5f), 1.5f);
    EXPECT_NEAR(input->PositionFactorFor(0.5f), 0.85f, 0.00001f);
    EXPECT_TRUE(NS::Obj::SerializeComponent(*input)["fields"].empty());
}

TEST(PlayerChargeParams, LiveParamsDriveTheJudgeCurvesAndTierBoundaries)
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
                                        {"中心近くの境目", 0.1f},
                                        {"チャージしきい値秒", 0.1f},
                                        {"チャージ満タン秒", 0.5f},
                                        {"チャージ倍率カーブ", {{"curve", {{0.0f, 2.0f}, {1.0f, 4.0f}}}}},
                                        {"突進位置係数カーブ", {{"curve", {{0.0f, 1.0f}, {1.0f, 0.5f}}}}}}),
              0u);
    EXPECT_NEAR(input->ChargingSpeedScale(), 0.6f, 0.00001f);
    EXPECT_EQ(input->HitTierFor(0.099f), NS::Game::Level::HitTier::Center);
    // 段は中心近くと大きな外れの 2 つ。中心近くの境目ちょうどから外は全部大きな外れ
    EXPECT_EQ(input->HitTierFor(0.1f), NS::Game::Level::HitTier::Wide);
    EXPECT_EQ(input->HitTierFor(0.5f), NS::Game::Level::HitTier::Wide);
    EXPECT_EQ(input->HitTierFor(0.8f), NS::Game::Level::HitTier::Wide);
    EXPECT_FLOAT_EQ(input->ChargeFactorFor(0.5f), 3.0f);
    EXPECT_FLOAT_EQ(input->PositionFactorFor(0.5f), 0.75f);
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
    entry["parts"] = {{"Params",
                       {{"チャージ減速率", 0.25f},
                        {"中心近くの境目", 0.2f},
                        {"チャージ倍率カーブ", {{"curve", {{0.0f, 1.0f}, {1.0f, 3.0f}}}}}}}};
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
    EXPECT_EQ(input->HitTierFor(0.19f), NS::Game::Level::HitTier::Center);
    EXPECT_EQ(input->HitTierFor(0.2f), NS::Game::Level::HitTier::Wide);
}
