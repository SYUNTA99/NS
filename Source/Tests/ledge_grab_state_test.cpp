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
    using NS::Scene::MovementState;

    constexpr float kFixedDt = 1.0f / 60.0f;

    /// 中心 (cx,cy,cz)・ 1m 立方の固形 block を表す AABB。
    NS::Core::AABB MakeBlock(float cx, float cy, float cz)
    {
        return NS::Core::AABB(NS::Core::Vector3{cx, cy, cz}, NS::Core::Vector3{0.5f, 0.5f, 0.5f});
    }

    void StepN(CharacterMovementComponent& mov, int n)
    {
        for (int i = 0; i < n; ++i)
            mov.OnUpdate();
    }
} // namespace

class LedgeGrabStateTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kFixedDt); }
};

TEST_F(LedgeGrabStateTest, GrabsLedgeWhenDescendingIntoEdge)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    const NS::Core::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    // block (上端 y=0.5) の -x 面手前、 手が上端付近に来る高さに置いて +x へ押す。
    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::LedgeHanging);
    // hang 位置: y は上端 - halfHeight、 x は面外側 (面 x=-0.5 から radius 分外)。
    EXPECT_NEAR(playerObj.Root().Position().y, 0.0f, 1e-3f);
    EXPECT_NEAR(playerObj.Root().Position().x, -0.9f, 1e-3f);
}

TEST_F(LedgeGrabStateTest, ClimbInputMantlesOntoBlockTop)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    const NS::Core::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);

    // 前入力 (climb 縦) で mantle。 block 上面 (y=0.5) より上に立つ。
    mov.SetClimbMove(0.0f, 1.0f);
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::Walking);
    EXPECT_TRUE(mov.IsGrounded());
    EXPECT_GT(playerObj.Root().Position().y, 0.5f);
}

TEST_F(LedgeGrabStateTest, JumpMantlesOntoBlockTop)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    const NS::Core::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);

    mov.SetJumpPressed();
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::Walking);
    EXPECT_TRUE(mov.IsGrounded());
    EXPECT_GT(playerObj.Root().Position().y, 0.5f);
}

TEST_F(LedgeGrabStateTest, BackInputDropsAndDoesNotReGrabImmediately)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    const NS::Core::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);

    // 後入力で手を放す → Falling。
    mov.SetClimbMove(0.0f, -1.0f);
    StepN(mov, 1);
    EXPECT_EQ(mov.State(), MovementState::Falling);

    // 放した直後に前を押し続けても、 cooldown 中は再掴みしない。
    mov.SetClimbMove(0.0f, 0.0f);
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 5);
    EXPECT_NE(mov.State(), MovementState::LedgeHanging);
}

TEST_F(LedgeGrabStateTest, DoesNotGrabWhileAscending)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    const NS::Core::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    // jump で上昇させると velocity.y > 0 になり、 上昇中は掴まない。
    mov.SetJumpPressed();
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::Jumping);
    EXPECT_NE(mov.State(), MovementState::LedgeHanging);
}
