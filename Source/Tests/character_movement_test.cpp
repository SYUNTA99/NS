#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <gtest/gtest.h>

#include <limits>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Object::CharacterMovementComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    //! デバッグ描画を切って N 回 OnUpdate を呼ぶ
    void StepN(CharacterMovementComponent& mov, int n)
    {
        mov.SetDebugDrawEnabled(false);
        for (int i = 0; i < n; ++i)
            mov.OnUpdate();
    }

    //! owner と world に床 1 枚を仕込み、 接地するまで step した CharacterMovementComponent を返す
    //! 物理世界なしだと永遠に空中なので、 接地ジャンプとコヨーテ猶予の検証にはこれで地面を与える
    CharacterMovementComponent& MakeGrounded(GameObject& owner, NS::Physics::PhysicsWorld& world)
    {
        auto& mov = *owner.AddComponent<CharacterMovementComponent>();
        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{8.0f, 0.5f, 8.0f}});
        world.BuildBroadphase();
        owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
        mov.SetPhysicsWorld(&world);
        mov.SetDebugDrawEnabled(false);
        for (int i = 0; i < 30 && !mov.IsGrounded(); ++i)
            mov.OnUpdate();
        return mov;
    }
} // namespace

class CharacterMovementTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

// 当たりの形は同居する CapsuleColliderComponent が正。写さないと Inspector で触っても移動に効かない
TEST_F(CharacterMovementTest, AdoptsSiblingCapsuleColliderSize)
{
    GameObject obj;
    obj.AddComponent<NS::Object::CapsuleColliderComponent>(0.7f, 0.9f);
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();
    mov.OnStart();
    StepN(mov, 1);

    EXPECT_FLOAT_EQ(mov.CapsuleRadius(), 0.7f);
    EXPECT_FLOAT_EQ(mov.CapsuleHalfHeight(), 0.9f);
}

TEST_F(CharacterMovementTest, GravityReducesVerticalVelocityWhenAirborne)
{
    GameObject obj;
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();

    const float vyBefore = mov.Velocity().y;
    StepN(mov, 1);
    const float vyAfter = mov.Velocity().y;
    EXPECT_LT(vyAfter, vyBefore);
}

TEST_F(CharacterMovementTest, JumpPressedAppliesImpulseAndConsumesOneJump)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);
    ASSERT_TRUE(mov.IsGrounded());

    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_GT(mov.Velocity().y, 5.0f);
    EXPECT_EQ(mov.JumpsRemaining(), 0);
}

TEST_F(CharacterMovementTest, NoJumpAfterCoyoteExpiresWhileAirborne)
{
    // 接地もコヨーテ窓も無い空中で press しても跳べない。 コヨーテを跳べる限界にした回帰
    GameObject obj;
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_LE(mov.Velocity().y, 0.0f);  // ジャンプの上向き初速は出ず重力で負のまま
    EXPECT_EQ(mov.JumpsRemaining(), 1); // ジャンプは消費されない
}

TEST_F(CharacterMovementTest, SecondJumpDoesNotFireWithoutLanding)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);

    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_EQ(mov.JumpsRemaining(), 0);

    // 着地していないので 2 回目 press は無視される
    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_EQ(mov.JumpsRemaining(), 0);
}

TEST_F(CharacterMovementTest, JumpReleaseHalvesVerticalVelocity)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);

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
    NS::Physics::PhysicsWorld world;
    auto& movA = MakeGrounded(obj, world);
    GameObject objB;
    auto& movB = *objB.AddComponent<CharacterMovementComponent>();

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
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);

    mov.SetJumpPressed();
    for (int i = 0; i < 10; ++i)
        mov.OnUpdate();

    EXPECT_LT(std::abs(mov.Velocity().y), 9.0f);
}

TEST_F(CharacterMovementTest, DesiredMoveAcceleratesHorizontalVelocity)
{
    GameObject obj;
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 30);

    EXPECT_GT(mov.Velocity().x, 5.0f);
}

TEST_F(CharacterMovementTest, CapsuleSettersPersist)
{
    CharacterMovementComponent mov;
    mov.SetCapsuleRadius(0.6f);
    mov.SetCapsuleHalfHeight(0.8f);
    EXPECT_FLOAT_EQ(mov.CapsuleRadius(), 0.6f);
    EXPECT_FLOAT_EQ(mov.CapsuleHalfHeight(), 0.8f);
}

TEST_F(CharacterMovementTest, OnUpdateNoOpWhenInactive)
{
    GameObject obj;
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();
    mov.SetActive(false);
    mov.SetDebugDrawEnabled(false);

    mov.SetJumpPressed();
    mov.OnUpdate();

    EXPECT_FLOAT_EQ(mov.Velocity().y, 0.0f);
}

TEST_F(CharacterMovementTest, SetMaxSpeedPersists)
{
    CharacterMovementComponent mov;
    mov.SetMaxSpeed(20.0f);
    EXPECT_FLOAT_EQ(mov.MaxSpeed(), 20.0f);
}

TEST_F(CharacterMovementTest, SetMaxSpeedClampsNegativeToZero)
{
    CharacterMovementComponent mov;
    mov.SetMaxSpeed(-1.0f);
    EXPECT_FLOAT_EQ(mov.MaxSpeed(), 0.0f);
}

TEST_F(CharacterMovementTest, SetMaxSpeedIgnoresNonFinite)
{
    CharacterMovementComponent mov;
    mov.SetMaxSpeed(20.0f);
    mov.SetMaxSpeed(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(mov.MaxSpeed(), 20.0f);

    mov.SetMaxSpeed(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(mov.MaxSpeed(), 20.0f);
}

TEST_F(CharacterMovementTest, DesiredSpeedScaleReadsBackLastInput)
{
    CharacterMovementComponent mov;
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 0.8f);
    EXPECT_FLOAT_EQ(mov.DesiredSpeedScale(), 0.8f);
}

TEST_F(CharacterMovementTest, DesiredDirectionReadsBackLastInput)
{
    CharacterMovementComponent mov;
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 0.8f);
    const Vector3 dir = mov.DesiredDirection();
    EXPECT_FLOAT_EQ(dir.x, 1.0f);
    EXPECT_FLOAT_EQ(dir.y, 0.0f);
    EXPECT_FLOAT_EQ(dir.z, 0.0f);
}

TEST_F(CharacterMovementTest, BodySlamStartsWhenGroundedInLocomotion)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);
    ASSERT_TRUE(mov.IsGrounded());

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(1.0f);
    StepN(mov, 1);

    EXPECT_TRUE(mov.IsBodySlamming());
    EXPECT_FLOAT_EQ(mov.BodySlamCharge01(), 1.0f);
    EXPECT_GT(mov.Velocity().x, 15.0f);
}

// 空中で押した発動が消えると連打で出ない時ができる。空中でも出す
TEST_F(CharacterMovementTest, BodySlamFiresInAir)
{
    GameObject obj;
    auto& mov = *obj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);
    ASSERT_FALSE(mov.IsGrounded());

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(1.0f);
    StepN(mov, 1);

    EXPECT_TRUE(mov.IsBodySlamming());
}

// 突進中の押しをその歩で捨てると連打が取りこぼされる。先行入力時間ぶん覚えて突進明けに出す
TEST_F(CharacterMovementTest, BodySlamRequestBuffersDuringRush)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(0.0f);
    StepN(mov, 1);
    ASSERT_TRUE(mov.IsBodySlamming());

    StepN(mov, 8);
    ASSERT_TRUE(mov.IsBodySlamming());
    mov.RequestBodySlam(1.0f);

    for (int i = 0; i < 120 && mov.BodySlamCharge01() < 1.0f; ++i)
        StepN(mov, 1);

    EXPECT_TRUE(mov.IsBodySlamming());
    EXPECT_FLOAT_EQ(mov.BodySlamCharge01(), 1.0f);
}

// 覚え続けると忘れた頃の着地で勝手に出るため、先行入力時間で失効させる
TEST_F(CharacterMovementTest, BodySlamRequestExpiresAfterBufferTime)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(1.0f);
    StepN(mov, 1);
    ASSERT_TRUE(mov.IsBodySlamming());
    mov.RequestBodySlam(0.5f);

    for (int i = 0; i < 120 && mov.IsBodySlamming(); ++i)
        StepN(mov, 1);

    EXPECT_FALSE(mov.IsBodySlamming());
    StepN(mov, 1);
    EXPECT_FALSE(mov.IsBodySlamming());
}

TEST_F(CharacterMovementTest, BodySlamEndsAfterRushDistance)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);
    const float startX = obj.Root().Position().x;

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(1.0f);
    StepN(mov, 1);
    ASSERT_TRUE(mov.IsBodySlamming());

    int steps = 0;
    while (mov.IsBodySlamming() && steps < 120)
    {
        StepN(mov, 1);
        ++steps;
    }

    EXPECT_LT(steps, 120);
    EXPECT_GT(steps, 5);
    EXPECT_GT(obj.Root().Position().x - startX, 5.0f);
}

TEST_F(CharacterMovementTest, BodySlamIgnoresDirectionInput)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(1.0f);
    StepN(mov, 1);
    ASSERT_TRUE(mov.IsBodySlamming());

    mov.SetDesiredMove({0.0f, 0.0f, 1.0f}, 1.0f);
    StepN(mov, 3);

    ASSERT_TRUE(mov.IsBodySlamming());
    EXPECT_GT(mov.Velocity().x, 15.0f);
    EXPECT_NEAR(mov.Velocity().z, 0.0f, 1.0e-4f);
}

TEST_F(CharacterMovementTest, BodySlamProgressRisesThenCancelResets)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);
    EXPECT_FLOAT_EQ(mov.BodySlamProgress01(), 0.0f);

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(1.0f);
    StepN(mov, 1);
    ASSERT_TRUE(mov.IsBodySlamming());

    float previous = mov.BodySlamProgress01();
    for (int i = 0; i < 5; ++i)
    {
        StepN(mov, 1);
        const float now = mov.BodySlamProgress01();
        EXPECT_GT(now, previous);
        previous = now;
    }

    mov.CancelBodySlam();
    EXPECT_FALSE(mov.IsBodySlamming());
    EXPECT_FLOAT_EQ(mov.BodySlamProgress01(), 0.0f);
}

TEST_F(CharacterMovementTest, TapBodySlamHopsForwardAndUp)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& mov = MakeGrounded(obj, world);

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    mov.RequestBodySlam(0.0f);
    StepN(mov, 1);

    ASSERT_TRUE(mov.IsBodySlamming());
    EXPECT_FLOAT_EQ(mov.BodySlamCharge01(), 0.0f);
    EXPECT_GT(mov.Velocity().y, 0.0f);
    EXPECT_GT(mov.Velocity().x, 5.0f);
    EXPECT_LT(mov.Velocity().x, 15.0f);
}
