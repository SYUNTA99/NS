#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Scene/CharacterMovementComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

#include <span>

namespace
{
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;

    constexpr float kFixedDt = 1.0f / 60.0f;

    /// 衝突なしの環境で N 回 OnUpdate を呼ぶ。debug draw は false 固定。
    void StepN(CharacterMovementComponent& mov, int n)
    {
        std::span<const NS::Core::AABB> empty;
        mov.SetCollisionWorld(empty);
        mov.SetDebugDrawEnabled(false);
        for (int i = 0; i < n; ++i)
            mov.OnUpdate();
    }
} // namespace

class CharacterMovementTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kFixedDt); }
};

TEST_F(CharacterMovementTest, GravityReducesVerticalVelocityWhenAirborne)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);

    const float vyBefore = mov.Velocity().y;
    StepN(mov, 1);
    const float vyAfter = mov.Velocity().y;
    EXPECT_LT(vyAfter, vyBefore);
}

TEST_F(CharacterMovementTest, JumpPressedAppliesImpulseAndConsumesOneJump)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);

    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_GT(mov.Velocity().y, 5.0f);
    EXPECT_EQ(mov.JumpsRemaining(), 0);
}

TEST_F(CharacterMovementTest, SecondJumpDoesNotFireWithoutLanding)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);

    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_EQ(mov.JumpsRemaining(), 0);

    // 着地していないので 2 回目 press は無視される。
    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_EQ(mov.JumpsRemaining(), 0);
}

TEST_F(CharacterMovementTest, JumpReleaseHalvesVerticalVelocity)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);

    mov.SetJumpPressed();
    mov.SetJumpHeld(true);
    StepN(mov, 1);

    const float vyHeld = mov.Velocity().y;
    EXPECT_GT(vyHeld, 0.0f);

    mov.SetJumpHeld(false);
    StepN(mov, 1);
    const float vyReleased = mov.Velocity().y;
    EXPECT_LT(vyReleased, vyHeld * 0.6f);
}

TEST_F(CharacterMovementTest, AsymmetricGravityIsStrongerOnDescent)
{
    GameObject obj;
    GameObject objB;
    CharacterMovementComponent movA(&obj);
    CharacterMovementComponent movB(&objB);

    movA.SetJumpPressed();
    StepN(movA, 5);
    const float vyAscending = movA.Velocity().y;

    StepN(movB, 1);
    const float vyDescentSmall = movB.Velocity().y;

    EXPECT_LT(std::abs(vyDescentSmall - 0.0f), std::abs(vyAscending));
}

TEST_F(CharacterMovementTest, ApexHangScalesGravity)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);
    mov.SetDebugDrawEnabled(false);
    std::span<const NS::Core::AABB> empty;
    mov.SetCollisionWorld(empty);

    mov.SetJumpPressed();
    for (int i = 0; i < 10; ++i)
        mov.OnUpdate();

    EXPECT_LT(std::abs(mov.Velocity().y), 9.0f);
}

TEST_F(CharacterMovementTest, DesiredMoveAcceleratesHorizontalVelocity)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 30);

    EXPECT_GT(mov.Velocity().x, 5.0f);
}

TEST_F(CharacterMovementTest, CapsuleSettersPersist)
{
    CharacterMovementComponent mov(nullptr);
    mov.SetCapsuleRadius(0.6f);
    mov.SetCapsuleHalfHeight(0.8f);
    EXPECT_FLOAT_EQ(mov.CapsuleRadius(), 0.6f);
    EXPECT_FLOAT_EQ(mov.CapsuleHalfHeight(), 0.8f);
}

TEST_F(CharacterMovementTest, OnUpdateNoOpWhenInactive)
{
    GameObject obj;
    CharacterMovementComponent mov(&obj);
    mov.SetActive(false);
    mov.SetDebugDrawEnabled(false);

    mov.SetJumpPressed();
    std::span<const NS::Core::AABB> empty;
    mov.SetCollisionWorld(empty);
    mov.OnUpdate();

    EXPECT_FLOAT_EQ(mov.Velocity().y, 0.0f);
}
