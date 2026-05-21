#include <gtest/gtest.h>

#include <ns/scene/components/character_movement_component.h>
#include <ns/scene/game_object.h>
#include <ns/scene/transform.h>

#include <span>

namespace
{
    using ns::scene::CharacterMovementComponent;
    using ns::scene::GameObject;

    constexpr float kFixedDt = 1.0f / 60.0f;

    /// 衝突なしの環境で N 回 OnUpdate を呼ぶ。debug draw は false 固定。
    void StepN(CharacterMovementComponent& mov, int n)
    {
        std::span<const ns::core::AABB> empty;
        mov.SetCollisionWorld(empty);
        mov.SetDebugDrawEnabled(false);
        for (int i = 0; i < n; ++i)
            mov.OnUpdate(kFixedDt);
    }
} // namespace

TEST(CharacterMovementTest, GravityReducesVerticalVelocityWhenAirborne)
{
    GameObject obj;
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);

    const float vyBefore = mov.Velocity().y;
    StepN(mov, 1);
    const float vyAfter = mov.Velocity().y;
    EXPECT_LT(vyAfter, vyBefore);
}

TEST(CharacterMovementTest, JumpPressedAppliesImpulseAndConsumesOneJump)
{
    GameObject obj;
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);

    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_GT(mov.Velocity().y, 5.0f);
    EXPECT_EQ(mov.JumpsRemaining(), 0);
}

TEST(CharacterMovementTest, SecondJumpDoesNotFireWithoutLanding)
{
    GameObject obj;
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);

    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_EQ(mov.JumpsRemaining(), 0);

    // 着地していないので 2 回目 press は無視される。
    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_EQ(mov.JumpsRemaining(), 0);
}

TEST(CharacterMovementTest, JumpReleaseHalvesVerticalVelocity)
{
    GameObject obj;
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);

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

TEST(CharacterMovementTest, AsymmetricGravityIsStrongerOnDescent)
{
    GameObject obj;
    CharacterMovementComponent movA;
    CharacterMovementComponent movB;
    GameObject objB;
    obj.RegisterComponent(&movA);
    objB.RegisterComponent(&movB);

    movA.SetJumpPressed();
    StepN(movA, 5);
    const float vyAscending = movA.Velocity().y;

    StepN(movB, 1);
    const float vyDescentSmall = movB.Velocity().y;

    EXPECT_LT(std::abs(vyDescentSmall - 0.0f), std::abs(vyAscending));
}

TEST(CharacterMovementTest, ApexHangScalesGravity)
{
    GameObject obj;
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);
    mov.SetDebugDrawEnabled(false);
    std::span<const ns::core::AABB> empty;
    mov.SetCollisionWorld(empty);

    mov.SetJumpPressed();
    for (int i = 0; i < 10; ++i)
        mov.OnUpdate(kFixedDt);

    EXPECT_LT(std::abs(mov.Velocity().y), 9.0f);
}

TEST(CharacterMovementTest, DesiredMoveAcceleratesHorizontalVelocity)
{
    GameObject obj;
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 30);

    EXPECT_GT(mov.Velocity().x, 5.0f);
}

TEST(CharacterMovementTest, CapsuleSettersPersist)
{
    CharacterMovementComponent mov;
    mov.SetCapsuleRadius(0.6f);
    mov.SetCapsuleHalfHeight(0.8f);
    EXPECT_FLOAT_EQ(mov.CapsuleRadius(), 0.6f);
    EXPECT_FLOAT_EQ(mov.CapsuleHalfHeight(), 0.8f);
}

TEST(CharacterMovementTest, OnUpdateNoOpWhenInactive)
{
    GameObject obj;
    CharacterMovementComponent mov;
    obj.RegisterComponent(&mov);
    mov.SetActive(false);
    mov.SetDebugDrawEnabled(false);

    mov.SetJumpPressed();
    std::span<const ns::core::AABB> empty;
    mov.SetCollisionWorld(empty);
    mov.OnUpdate(kFixedDt);

    EXPECT_FLOAT_EQ(mov.Velocity().y, 0.0f);
}
