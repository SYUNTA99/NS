#include "Game/Player.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneCamera.h"
#include "NSlib/Windows/Clock.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    // 欄の秒を固定ステップのフレーム数にする。Player::AdvanceCharge と同じ丸め
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::OS::FrameTimer::FixedDelta()));
    }
} // namespace

// 押したフレームに丸まり、しきい値のフレームに減速して横を止め、放したフレームに発動する
TEST(PlayerChargeSequence, PressThresholdAndReleaseKeepTheSameFrameOrder)
{
    Player player;
    player.Init();
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"チャージしきい値秒", 0.2f}, {"チャージ満タン秒", 1.0f}}),
              0u);
    const int threshold = FramesFor(0.2f);
    const int full = FramesFor(1.0f);
    NS::Obj::Body& movement = player.Body();
    movement.SetVelocity(NS::Vector3{3.0f, 5.0f, 4.0f});
    const float maxSpeed = player.MaxSpeed();

    player.Update(true);
    int held = 1;
    EXPECT_TRUE(player.ChargeJudge().JustPressed());
    EXPECT_FALSE(player.ChargeJudge().IsCharging());
    EXPECT_TRUE(player.IsCurled());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), maxSpeed);
    EXPECT_GT(movement.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.97f);

    while (held < threshold - 1)
    {
        player.Update(true);
        ++held;
        EXPECT_FALSE(player.ChargeJudge().IsCharging());
    }
    movement.SetVelocity(NS::Vector3{3.0f, 5.0f, 4.0f});
    player.Update(true);
    ++held;
    EXPECT_TRUE(player.ChargeJudge().JustStartedCharging());
    EXPECT_NEAR(player.MaxSpeed(), maxSpeed * 0.3f, 0.00001f);
    EXPECT_FLOAT_EQ(movement.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(movement.Velocity().z, 0.0f);
    // 縦は残す。0 を書くと同じフレームの重力で下向きになる
    EXPECT_GT(movement.Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.95f);

    // 横を止めるのはしきい値のフレームだけ
    movement.SetVelocity(NS::Vector3{2.0f, 5.0f, 1.0f});
    player.Update(true);
    ++held;
    EXPECT_GT(movement.Velocity().x, 0.0f);
    while (held < (threshold + full) / 2)
    {
        player.Update(true);
        ++held;
    }
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), 0.5f);
    player.Update(false);
    EXPECT_FALSE(player.ChargeJudge().IsHeld());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), maxSpeed);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 1.0f);
    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.5f);
}

TEST(PlayerChargeSequence, TapAndRepeatedPressDoNotSkipOrDuplicateAFrame)
{
    Player player;
    player.Init();
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    const int threshold = FramesFor(0.2f);
    player.Update(true);
    player.SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.Update(false);
    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.0f);
    player.ResetState();
    player.Update(false);
    EXPECT_FALSE(player.IsBodySlamming());
    player.Update(true);
    EXPECT_TRUE(player.ChargeJudge().JustPressed());
    EXPECT_FALSE(player.ChargeJudge().IsCharging());
    for (int held = 2; held < threshold; ++held)
    {
        player.Update(true);
        EXPECT_FALSE(player.ChargeJudge().IsCharging());
    }
    player.Update(true);
    EXPECT_TRUE(player.ChargeJudge().JustStartedCharging());
}

TEST(PlayerChargeSequence, LiveTimingAndRestartPreserveHeldDuration)
{
    Player player;
    player.Init();
    GL::Player::PlayerParams* params =
        NS::Obj::Cast<GL::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    player.Update(true);
    player.RestartFrom(NS::Obj::MakeSceneJson());
    EXPECT_FALSE(player.IsCurled());
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"チャージしきい値秒", 0.1f}, {"チャージ満タン秒", 0.3f}}), 0u);
    // 出直しの前に押した 1 フレームも数え、しきい値と満タンの真ん中まで押す
    const int threshold = FramesFor(0.1f);
    const int full = FramesFor(0.3f);
    for (int held = 1; held < (threshold + full) / 2; ++held)
    {
        player.Update(true);
    }
    EXPECT_TRUE(player.IsCurled());
    EXPECT_TRUE(player.ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), 0.5f);
    player.SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.Update(false);
    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.5f);
}

TEST(PlayerChargeSequence, ChargeCountsEveryFrameThroughARestartWhileHeld)
{
    Player player;
    player.Init();
    GL::Player::PlayerParams* params =
        NS::Obj::Cast<GL::Player::PlayerParams>(player.FindSubObj("Params"));
    ASSERT_NE(params, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"チャージしきい値秒", 0.1f}, {"チャージ満タン秒", 1.0f}}), 0u);
    // 溜め量は (押したフレーム数 − しきい値) / (満タン − しきい値)
    const int threshold = FramesFor(0.1f);
    const int full = FramesFor(1.0f);
    const float span = static_cast<float>(full - threshold);
    for (int frame = 0; frame < threshold * 3; ++frame)
    {
        player.Update(true);
    }
    player.RestartFrom(NS::Obj::MakeSceneJson());
    for (int frame = 0; frame < threshold * 2; ++frame)
    {
        player.Update(true);
    }
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), static_cast<float>(threshold * 4) / span);
    player.Die();
    player.Appear();
    for (int frame = 0; frame < threshold * 2; ++frame)
    {
        player.Update(true);
    }
    EXPECT_TRUE(player.ChargeJudge().IsHeld());
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), static_cast<float>(threshold * 6) / span);
}

// 構えは縦の倍率を答えるだけで、根のスケールは押しても溜めても放しても配置の値のまま
TEST(PlayerChargeSequence, StanceLeavesTheRootScaleAndAnswersTheHeight)
{
    Player player;
    player.Init();
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    const NS::Vector3 one{1.0f, 1.0f, 1.0f};
    player.Update(true);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.97f);
    EXPECT_TRUE(player.Root().Scale() == one);
    for (int held = 1; held < FramesFor(0.2f); ++held)
    {
        player.Update(true);
        SCOPED_TRACE(held);
        EXPECT_TRUE(player.Root().Scale() == one);
    }
    ASSERT_TRUE(player.ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player.StanceHeight(), 0.95f);
    player.Update(false);
    EXPECT_FLOAT_EQ(player.StanceHeight(), 1.0f);
    EXPECT_TRUE(player.Root().Scale() == one);
}

TEST(PlayerChargeSequence, EndPlayWhileHeldRestoresStanceAndUncurls)
{
    Player player;
    player.Init();
    player.Appearance().OnStart();
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    for (int frame = 0; frame < FramesFor(0.2f); ++frame)
    {
        player.Update(true);
    }
    ASSERT_FLOAT_EQ(player.ModelSubObj()->DrawScale().y, 0.95f);
    player.OnEndPlay();
    EXPECT_FLOAT_EQ(player.StanceHeight(), 1.0f);
    EXPECT_TRUE(player.ModelSubObj()->DrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    EXPECT_FALSE(player.IsCurled());
}

TEST(PlayerChargeSequence, ChargedReleaseUsesTheLastHeldAimBeforeClearingIt)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(doc);
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    TestViewCamera* camera = PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_NE(camera, nullptr);
    for (int frame = 0; frame < FramesFor(0.2f); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    GL::Level::AimLine aim{};
    ASSERT_TRUE(player->TryGetAimLine(aim));
    EXPECT_FLOAT_EQ(aim.direction.z, 1.0f);
    camera->SetPose(NS::Vector3{}, NS::Vector3{1.0f, 0.0f, 0.0f});
    player->Update(false);
    EXPECT_FALSE(player->TryGetAimLine(aim));
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_FLOAT_EQ(player->BodySlamDirection().x, 0.0f);
    EXPECT_FLOAT_EQ(player->BodySlamDirection().z, 1.0f);
}

// 狙いの線は遊びの視点 (仮想カメラの合成) に沿う。描画が書いた実カメラの向きは読まない
TEST(PlayerChargeSequence, AimLineFollowsTheViewCameraNotTheDrawnCamera)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(doc);
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{-5.0f, 0.0f, 0.0f}, NS::Vector3{}), nullptr);
    NS::Obj::SceneCamera* camera = scene.MainCamera();
    ASSERT_NE(camera, nullptr);
    camera->SetPosition(NS::Vector3{});
    camera->SetTarget(NS::Vector3{0.0f, 0.0f, 1.0f});
    for (int frame = 0; frame < FramesFor(0.2f); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());

    GL::Level::AimLine aim{};
    ASSERT_TRUE(player->TryGetAimLine(aim));
    EXPECT_FLOAT_EQ(aim.direction.x, 1.0f);
    EXPECT_FLOAT_EQ(aim.direction.z, 0.0f);
}
