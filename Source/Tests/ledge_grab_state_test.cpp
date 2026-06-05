#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Scene/CharacterMovementComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

#include <cmath>
#include <span>

namespace
{
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;
    using NS::Scene::MovementState;

    constexpr float kFixedDt = 1.0f / 60.0f;

    /// 中心 (cx,cy,cz)・ 1m 立方の固形 block を表す AABB
    NS::Math::AABB MakeBlock(float cx, float cy, float cz)
    {
        return NS::Math::AABB(NS::Math::Vector3{cx, cy, cz}, NS::Math::Vector3{0.5f, 0.5f, 0.5f});
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
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    const NS::Math::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    // block (上端 y=0.5) の -x 面手前、 手が上端付近に来る高さに置いて +x へ押す
    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::LedgeHanging);
    // hang 位置: y は上端 - halfHeight、 x は面外側 (面 x=-0.5 から radius 分外)
    EXPECT_NEAR(playerObj.Root().Position().y, 0.0f, 1e-3f);
    EXPECT_NEAR(playerObj.Root().Position().x, -0.9f, 1e-3f);
}

TEST_F(LedgeGrabStateTest, ClimbInputMantlesOntoBlockTop)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    const NS::Math::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);

    // 前入力 (climb 縦) を保持。 最小ぶら下がり時間 (0.3s ≒ 18 frame) を過ぎると自動で mantle
    // 乗り上がり後に縁から歩き落ちないよう、 通常移動入力は止めておく
    mov.SetClimbMove(0.0f, 1.0f);
    mov.SetDesiredMove({0.0f, 0.0f, 0.0f}, 0.0f);

    // 待ち時間内 (1 frame) ではまだぶら下がったまま
    StepN(mov, 1);
    EXPECT_EQ(mov.State(), MovementState::LedgeHanging);

    // 待ち時間 (0.3s) + 乗り上がりモーション (0.25s) を過ぎれば block 上面 (y=0.5) より上に立つ
    StepN(mov, 45);
    EXPECT_EQ(mov.State(), MovementState::Walking);
    EXPECT_TRUE(mov.IsGrounded());
    EXPECT_GT(playerObj.Root().Position().y, 0.5f);
}

TEST_F(LedgeGrabStateTest, JumpMantlesOntoBlockTop)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    const NS::Math::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);

    // ジャンプは待ち時間なしで mantle を開始するが、 即立ちではなくモーションに入る
    mov.SetJumpPressed();
    StepN(mov, 1);
    EXPECT_EQ(mov.State(), MovementState::LedgeMantling);

    // 乗り上がり後に縁から歩き落ちないよう移動入力は止める
    mov.SetDesiredMove({0.0f, 0.0f, 0.0f}, 0.0f);
    StepN(mov, 20);
    EXPECT_EQ(mov.State(), MovementState::Walking);
    EXPECT_TRUE(mov.IsGrounded());
    EXPECT_GT(playerObj.Root().Position().y, 0.5f);
}

TEST_F(LedgeGrabStateTest, MantleRisesGraduallyNotInstant)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    const NS::Math::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);
    const float yHang = playerObj.Root().Position().y;

    mov.SetJumpPressed();
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeMantling);

    // モーション途中: まだ立ち上がりきっておらず、 y は hang から上昇している
    mov.SetDesiredMove({0.0f, 0.0f, 0.0f}, 0.0f);
    StepN(mov, 3);
    EXPECT_EQ(mov.State(), MovementState::LedgeMantling);
    EXPECT_GT(playerObj.Root().Position().y, yHang);

    // 完了すれば立つ
    StepN(mov, 20);
    EXPECT_EQ(mov.State(), MovementState::Walking);
}

TEST_F(LedgeGrabStateTest, BackInputDropsAndDoesNotReGrabImmediately)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    const NS::Math::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);

    // 後入力で手を放す → Falling
    mov.SetClimbMove(0.0f, -1.0f);
    StepN(mov, 1);
    EXPECT_EQ(mov.State(), MovementState::Falling);

    // 放した直後に前を押し続けても、 cooldown 中は再掴みしない
    mov.SetClimbMove(0.0f, 0.0f);
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 5);
    EXPECT_NE(mov.State(), MovementState::LedgeHanging);
}

TEST_F(LedgeGrabStateTest, DoesNotGrabWhileAscending)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    const NS::Math::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    // jump で上昇させると velocity.y > 0 になり、 上昇中は掴まない
    mov.SetJumpPressed();
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::Jumping);
    EXPECT_NE(mov.State(), MovementState::LedgeHanging);
}

TEST_F(LedgeGrabStateTest, ShimmyMovesAlongLedge)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    // -x 面の縁が z 方向に 3 マス続く壁。 左右どちらへでも縁が続く
    const NS::Math::AABB world[] = {
        MakeBlock(0.0f, 0.0f, 0.0f),
        MakeBlock(0.0f, 0.0f, 1.0f),
        MakeBlock(0.0f, 0.0f, -1.0f),
    };
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);
    const float zStart = playerObj.Root().Position().z;

    // 左右入力で縁に沿ってシミー。 隣のマス側へ明確に動く
    mov.SetDesiredMove({0.0f, 0.0f, 0.0f}, 0.0f);
    mov.SetClimbMove(1.0f, 0.0f);
    StepN(mov, 20);

    EXPECT_EQ(mov.State(), MovementState::LedgeHanging);
    EXPECT_GT(std::abs(playerObj.Root().Position().z - zStart), 0.4f);
}

TEST_F(LedgeGrabStateTest, ShimmyStopsAtLedgeEnd)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    // 1 マスだけの縁。 端まで来たらそれ以上シミーできず、 落ちもしない
    const NS::Math::AABB world[] = {MakeBlock(0.0f, 0.0f, 0.0f)};
    mov.SetCollisionWorld(world);

    playerObj.Root().SetPosition({-0.9f, 0.0f, 0.0f});
    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::LedgeHanging);

    mov.SetDesiredMove({0.0f, 0.0f, 0.0f}, 0.0f);
    mov.SetClimbMove(1.0f, 0.0f);
    StepN(mov, 60);

    // 縁に留まったまま (落ちていない)、 block の z 範囲 (±0.5) を大きく超えない
    EXPECT_EQ(mov.State(), MovementState::LedgeHanging);
    EXPECT_LE(std::abs(playerObj.Root().Position().z), 0.55f);
}
