#include "Game/Player.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/PlayerInput.h"

#include <gtest/gtest.h>

TEST(PlayerInputOwnership, MovementReadsTheInputWithoutAnActorRelayTick)
{
    Player player;
    NS::Obj::PlayerInput* input = NS::Obj::ComponentCast<NS::Obj::PlayerInput>(player.Part("Input"));
    ASSERT_NE(input, nullptr);
    NS::Obj::Body& movement = player.Body();
    input->SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    movement.SetGrounded(true);
    player.AccelerateToInputDirection(0.1f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().x, 1.0f);
    EXPECT_NEAR(movement.LateralVelocity().x, 4.0f, 0.00001f);
    input->SetDesiredMove(NS::Core::Vector3{-1.0f, 0.0f, 0.0f}, 0.5f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().x, -1.0f);
    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 0.5f);
    input->SetClimbMove(2.0f, -2.0f);
    EXPECT_FLOAT_EQ(player.ClimbRight(), 1.0f);
    EXPECT_FLOAT_EQ(player.ClimbForward(), -1.0f);
    EXPECT_EQ(player.Phase(), NS::Obj::UpdatePhase::Player);
}

TEST(PlayerInputOwnership, PressIsKeptUntilMovementConsumesItAndCannotRepeat)
{
    Player player;
    NS::Obj::PlayerInput* input = NS::Obj::ComponentCast<NS::Obj::PlayerInput>(player.Part("Input"));
    ASSERT_NE(input, nullptr);
    NS::Obj::Body& movement = player.Body();
    movement.OnStart();
    movement.SetGrounded(true);
    player.SyncGroundState();
    input->SetJumpPressed();
    input->SetReleaseLedgePressed();
    input->OnUpdate();
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
    Player player;
    NS::Obj::PlayerInput* input = NS::Obj::ComponentCast<NS::Obj::PlayerInput>(player.Part("Input"));
    ASSERT_NE(input, nullptr);
    NS::Obj::Body& movement = player.Body();
    movement.OnStart();
    input->SetJumpHeld(true);
    player.Update();
    movement.SetVerticalVelocity(10.0f);
    input->SetJumpHeld(false);
    player.Update();
    EXPECT_LT(movement.VerticalVelocity(), 6.0f);
    const float afterRelease = movement.VerticalVelocity();
    player.Update();
    EXPECT_GT(movement.VerticalVelocity(), afterRelease * 0.6f);
    input->SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    input->SetJumpPressed();
    player.ResetState();
    EXPECT_FLOAT_EQ(input->DesiredSpeedScale(), 0.0f);
    EXPECT_FALSE(input->JumpPressed());
    EXPECT_FALSE(input->JumpHeld());
}
