#include "Game/Level/CollisionInput.h"
#include "Game/Player.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

TEST(PlayerChargeSequence, PressThresholdAndReleaseKeepTheSameFrameOrder)
{
    Player player;
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl"));
    ASSERT_NE(input, nullptr);
    input->OnStart();
    NS::Obj::Body& movement = player.Body();
    movement.SetVelocity(NS::Core::Vector3{3.0f, 5.0f, 4.0f});
    const float maxSpeed = player.MaxSpeed();

    input->Step(true, 0.1f);
    EXPECT_TRUE(input->Judge().JustPressed());
    EXPECT_FALSE(input->IsCharging());
    EXPECT_TRUE(player.IsCurled());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), maxSpeed);
    EXPECT_FLOAT_EQ(movement.Velocity().x, 3.0f);
    EXPECT_FLOAT_EQ(player.Root().Scale().y, 0.97f);

    input->Step(true, 0.1f);
    EXPECT_TRUE(input->Judge().JustStartedCharging());
    EXPECT_NEAR(player.MaxSpeed(), maxSpeed * 0.3f, 0.00001f);
    EXPECT_FLOAT_EQ(movement.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(movement.Velocity().y, 5.0f);
    EXPECT_FLOAT_EQ(movement.Velocity().z, 0.0f);
    EXPECT_FLOAT_EQ(player.Root().Scale().y, 0.95f);

    movement.SetVelocity(NS::Core::Vector3{2.0f, 5.0f, 1.0f});
    for (int step = 2; step < 6; ++step)
    {
        input->Step(true, 0.1f);
    }
    EXPECT_FLOAT_EQ(movement.Velocity().x, 2.0f);
    EXPECT_FLOAT_EQ(input->Judge().Charge01(), 0.5f);
    input->Step(false, 0.1f);
    EXPECT_FALSE(input->Judge().IsHeld());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), maxSpeed);
    EXPECT_FLOAT_EQ(player.Root().Scale().y, 1.0f);
    EXPECT_TRUE(player.BodySlam());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.5f);
}

TEST(PlayerChargeSequence, TapAndRepeatedPressDoNotSkipOrDuplicateAFrame)
{
    Player player;
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl"));
    ASSERT_NE(input, nullptr);
    input->OnStart();
    input->Step(true, 0.1f);
    player.SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    input->Step(false, 0.1f);
    EXPECT_TRUE(player.BodySlam());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.0f);
    player.ResetState();
    input->Step(false, 0.1f);
    EXPECT_EQ(input->Judge().TakeFired(), NS::Game::Level::SlamKind::None);
    input->Step(true, 0.1f);
    EXPECT_TRUE(input->Judge().JustPressed());
    EXPECT_FALSE(input->IsCharging());
    input->Step(true, 0.1f);
    EXPECT_TRUE(input->Judge().JustStartedCharging());
}

TEST(PlayerChargeSequence, LiveTimingAndRestartPreserveHeldDuration)
{
    Player player;
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl"));
    NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player.Part("Params"));
    ASSERT_NE(input, nullptr);
    ASSERT_NE(params, nullptr);
    input->OnStart();
    input->Step(true, 0.1f);
    player.RestartFrom(NS::Obj::MakeSceneJson());
    EXPECT_FALSE(player.IsCurled());
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"チャージしきい値秒", 0.1f}, {"チャージ満タン秒", 0.3f}}), 0u);
    input->Step(true, 0.1f);
    EXPECT_TRUE(player.IsCurled());
    EXPECT_TRUE(input->IsCharging());
    EXPECT_FLOAT_EQ(input->Judge().Charge01(), 0.5f);
    player.SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    input->Step(false, 0.1f);
    EXPECT_TRUE(player.BodySlam());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.5f);
}

TEST(PlayerChargeSequence, ChargeCountsEveryFrameThroughARestartWhileHeld)
{
    Player player;
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl"));
    NS::Game::Player::PlayerParams* params =
        NS::Obj::ComponentCast<NS::Game::Player::PlayerParams>(player.Part("Params"));
    ASSERT_NE(input, nullptr);
    ASSERT_NE(params, nullptr);
    // 0.1 秒の刻みで、しきい値 1 フレーム・満タン 10 フレーム。溜め量は (押したフレーム数 − 1) / 9
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"チャージしきい値秒", 0.1f}, {"チャージ満タン秒", 1.0f}}), 0u);
    input->OnStart();
    for (int step = 0; step < 3; ++step)
    {
        input->Step(true, 0.1f);
    }
    player.RestartFrom(NS::Obj::MakeSceneJson());
    for (int step = 0; step < 2; ++step)
    {
        input->Step(true, 0.1f);
    }
    EXPECT_FLOAT_EQ(input->Judge().Charge01(), 4.0f / 9.0f);
    player.Die();
    player.Appear();
    for (int step = 0; step < 2; ++step)
    {
        input->Step(true, 0.1f);
    }
    EXPECT_TRUE(input->Judge().IsHeld());
    EXPECT_FLOAT_EQ(input->Judge().Charge01(), 6.0f / 9.0f);
}

TEST(PlayerChargeSequence, EndPlayRestoresStanceBeforeThePlayerPartsLeave)
{
    Player player;
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl"));
    ASSERT_NE(input, nullptr);
    input->OnStart();
    input->Step(true, 0.1f);
    input->Step(true, 0.1f);
    EXPECT_FLOAT_EQ(player.Root().Scale().y, 0.95f);
    player.OnEndPlay();
    EXPECT_FLOAT_EQ(player.Root().Scale().y, 1.0f);
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
    NS::Game::Level::CollisionInput* input =
        NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player->Part("ChargeControl"));
    ASSERT_NE(input, nullptr);
    input->OnStart();
    NS::Obj::CameraComponent* camera = scene.MainCamera();
    ASSERT_NE(camera, nullptr);
    camera->SetPosition(NS::Core::Vector3{});
    camera->SetTarget(NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    for (int step = 0; step < 6; ++step)
    {
        input->Step(true, 0.1f);
    }
    NS::Game::Level::AimLine aim{};
    ASSERT_TRUE(input->TryGetAimLine(aim));
    EXPECT_FLOAT_EQ(aim.direction.z, 1.0f);
    camera->SetTarget(NS::Core::Vector3{1.0f, 0.0f, 0.0f});
    input->Step(false, 0.1f);
    EXPECT_FALSE(input->TryGetAimLine(aim));
    EXPECT_TRUE(player->BodySlam());
    EXPECT_FLOAT_EQ(player->BodySlamDirection().x, 0.0f);
    EXPECT_FLOAT_EQ(player->BodySlamDirection().z, 1.0f);
}
