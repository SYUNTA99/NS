#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Input.h"

#include <gtest/gtest.h>

TEST(PlayerInputOwnership, MovementReadsTheInputWithoutAnActorRelayTick)
{
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    NS::PlayerInput* input = &player.Input();
    NS::Body& movement = player.Body();
    input->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    movement.SetGrounded(true);
    player.AccelerateToInputDirection(0.1f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().x, 1.0f);
    EXPECT_NEAR(movement.LateralVelocity().x, 4.0f, 0.00001f);
    input->SetDesiredMove(NS::Vector3{-1.0f, 0.0f, 0.0f}, 0.5f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().x, -1.0f);
    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 0.5f);
    input->SetClimbMove(2.0f, -2.0f);
    EXPECT_FLOAT_EQ(player.ClimbRight(), 1.0f);
    EXPECT_FLOAT_EQ(player.ClimbForward(), -1.0f);
}

TEST(PlayerInputOwnership, PressIsKeptUntilMovementConsumesItAndCannotRepeat)
{
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    NS::PlayerInput* input = &player.Input();
    NS::Body& movement = player.Body();
    movement.SetGrounded(true);
    player.SyncGroundState();
    input->SetJumpPressed();
    input->SetReleaseLedgePressed();
    input->Update(&player);
    ASSERT_TRUE(input->JumpPressed());
    ASSERT_TRUE(input->ReleaseLedgePressed());
    player.Update();
    EXPECT_GT(movement.VerticalVelocity(), 0.0f);
    EXPECT_FALSE(input->JumpPressed());
    EXPECT_FALSE(input->ReleaseLedgePressed());
    movement.SetGrounded(true);
    player.SyncGroundState();
    movement.SetVerticalVelocity(0.0f);
    player.Update();
    EXPECT_LE(movement.VerticalVelocity(), 0.0f);
}

TEST(PlayerInputOwnership, RestartClearsSharedInputAndHeldJumpCutsOnlyOnce)
{
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    NS::PlayerInput* input = &player.Input();
    NS::Body& movement = player.Body();
    input->SetJumpHeld(true);
    player.Update(false);
    movement.SetVerticalVelocity(10.0f);
    input->SetJumpHeld(false);
    player.Update(false);
    EXPECT_LT(movement.VerticalVelocity(), 6.0f);
    const float afterRelease = movement.VerticalVelocity();
    player.Update(false);
    EXPECT_GT(movement.VerticalVelocity(), afterRelease * 0.6f);
    input->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    input->SetJumpPressed();
    player.ResetState();
    EXPECT_FLOAT_EQ(input->DesiredSpeedScale(), 0.0f);
    EXPECT_FALSE(input->JumpPressed());
    EXPECT_FALSE(input->JumpHeld());
}

TEST(PlayerInputOwnership, SlamTappedBetweenPlayerStepsIsSeenOnce)
{
    // 遅い世界では自機が読まない固定フレームがある。その間に押して放した突進を、次に読む歩で 1 回押したと見せる
    NS::OS::Input& platform = NS::OS::Input::Get();
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    NS::PlayerInput* input = &player.Input();
    platform.Mouse().OnButtonDown(NS::OS::MouseButton::Left);
    platform.Mouse().OnButtonUp(NS::OS::MouseButton::Left);
    input->Update(&player);
    EXPECT_TRUE(input->SlamHeld());
    input->ConsumePressed();
    input->Update(&player);
    EXPECT_FALSE(input->SlamHeld());
}

TEST(PlayerInputOwnership, JumpTappedBetweenPlayerUpdatesIsReadByTheNextUpdate)
{
    // 自機は自分の更新の中で機器を読む。前の更新から後に押して離した跳びを、次の更新が拾う
    NS::OS::Input& platform = NS::OS::Input::Get();
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    NS::Body& movement = player.Body();
    movement.SetGrounded(true);
    player.SyncGroundState();
    platform.Keyboard().OnKeyDown(NS::OS::Key::Space);
    platform.Keyboard().OnKeyUp(NS::OS::Key::Space);
    player.Update();
    EXPECT_GT(movement.VerticalVelocity(), 0.0f);
}

TEST(PlayerInputOwnership, PressDuringALockWithNoReadIsNotSeenAfterUnlock)
{
    // 止めている間に世界が進まず読まなかった押しも、止めを解いた後の跳びにしない
    NS::OS::Input& platform = NS::OS::Input::Get();
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    NS::PlayerInput* input = &spawned->Input();
    input->SetLocked(true);
    platform.Keyboard().OnKeyDown(NS::OS::Key::Space);
    platform.Keyboard().OnKeyUp(NS::OS::Key::Space);
    input->SetLocked(false);
    input->Update(spawned);
    EXPECT_FALSE(input->JumpPressed());
}

TEST(PlayerInputOwnership, JumpPressedDuringHitStopIsKeptUntilTheStateRuns)
{
    // 当たりの止めの間は状態を進めないので、止めの間に読んだ押しを下ろさずに止めの明けへ持ち越す
    NS::OS::Input& platform = NS::OS::Input::Get();
    NS::Scene scene;
    PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(spawned, nullptr);
    PlayerActor& player = *spawned;
    player.RequestHitStop(2);
    platform.Keyboard().OnKeyDown(NS::OS::Key::Space);
    platform.Keyboard().OnKeyUp(NS::OS::Key::Space);
    player.Update();
    EXPECT_TRUE(player.Input().JumpPressed());
}
