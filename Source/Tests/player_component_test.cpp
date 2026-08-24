#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStatsManagerComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStatsManagerComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;
} // namespace

class PlayerComponentTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

TEST_F(PlayerComponentTest, DesiredSpeedScaleClampsToUnitRange)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 2.0f);

    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 1.0f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().x, 1.0f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().z, 0.0f);
}

TEST_F(PlayerComponentTest, ClimbMoveClampsToSignedUnitRange)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.SetClimbMove(5.0f, -5.0f);

    EXPECT_FLOAT_EQ(player.ClimbRight(), 1.0f);
    EXPECT_FLOAT_EQ(player.ClimbForward(), -1.0f);
}

TEST_F(PlayerComponentTest, MaxSpeedRoundsNegativeAndDropsNonFinite)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.SetMaxSpeed(-3.0f);
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 0.0f);

    player.SetMaxSpeed(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 0.0f);

    player.SetMaxSpeed(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 0.0f);
}

// 調整値の読みは同居の組が正。ここが切れると Inspector で触っても手触りが変わらない
TEST_F(PlayerComponentTest, ReadsTuningFromSiblingStatsManager)
{
    GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.OnStart();
    EXPECT_FLOAT_EQ(player.CoyoteTime(), 0.025f);

    stats.SetCoyoteTime(0.2f);
    EXPECT_FLOAT_EQ(player.CoyoteTime(), 0.2f);
}

// 組を積まない検証台でも既定の組で動く。既定値は組の既定と同じなので手触りは変わらない
TEST_F(PlayerComponentTest, FallsBackToDefaultTuningWithoutStatsManager)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.OnStart();

    EXPECT_FLOAT_EQ(player.CoyoteTime(), 0.025f);
    EXPECT_FLOAT_EQ(player.Stats().walkSpeed, 4.0f);
}

TEST_F(PlayerComponentTest, ResetStateClearsMotion)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetVelocity(Vector3{3.0f, 9.0f, -2.0f});
    player.SetGrounded(true);
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);

    player.ResetState();

    EXPECT_FLOAT_EQ(player.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player.Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player.Velocity().z, 0.0f);
    EXPECT_EQ(player.JumpsRemaining(), 1);
    EXPECT_FALSE(player.IsGrounded());
    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 0.0f);
}
