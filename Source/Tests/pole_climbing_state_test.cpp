#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Scene/CharacterMovementComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/PoleComponent.h>
#include <Framework/Scene/Transform.h>

#include <array>
#include <span>

namespace
{
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;
    using NS::Scene::MovementState;
    using NS::Scene::PoleComponent;

    constexpr float kFixedDt = 1.0f / 60.0f;

    void StepN(CharacterMovementComponent& mov, int n)
    {
        for (int i = 0; i < n; ++i)
            mov.OnUpdate();
    }
} // namespace

class PoleClimbingStateTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kFixedDt); }
};

TEST_F(PoleClimbingStateTest, EnterStateWhenOverlapping)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    GameObject poleObj;
    poleObj.Root().SetPosition({0.0f, 1.0f, 0.0f});
    auto& pole = *poleObj.AddComponent<PoleComponent>(0.2f, 2.0f);

    playerObj.Root().SetPosition({0.0f, 1.0f, 0.0f});

    PoleComponent* polePtrs[] = {&pole};
    mov.SetClimbables(std::span<PoleComponent* const>{polePtrs});

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::ClimbingPole);
    EXPECT_EQ(mov.AttachedPole(), &pole);
}

TEST_F(PoleClimbingStateTest, VerticalInputMovesPlayer)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    GameObject poleObj;
    poleObj.Root().SetPosition({0.0f, 1.0f, 0.0f});
    auto& pole = *poleObj.AddComponent<PoleComponent>(0.2f, 2.0f);

    playerObj.Root().SetPosition({0.0f, 0.5f, 0.0f});

    PoleComponent* polePtrs[] = {&pole};
    mov.SetClimbables(std::span<PoleComponent* const>{polePtrs});

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::ClimbingPole);

    const float yBefore = playerObj.Root().Position().y;

    // climb 縦移動は専用チャンネル (生ローカル前後入力) で与える
    mov.SetClimbMove(0.0f, 1.0f);
    StepN(mov, 5);

    const float yAfter = playerObj.Root().Position().y;
    EXPECT_GT(yAfter, yBefore);
}

TEST_F(PoleClimbingStateTest, JumpPressExits)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    GameObject poleObj;
    poleObj.Root().SetPosition({0.0f, 1.0f, 0.0f});
    auto& pole = *poleObj.AddComponent<PoleComponent>(0.2f, 2.0f);

    playerObj.Root().SetPosition({0.0f, 0.5f, 0.0f});

    PoleComponent* polePtrs[] = {&pole};
    mov.SetClimbables(std::span<PoleComponent* const>{polePtrs});

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::ClimbingPole);

    mov.SetJumpPressed();
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::Falling);
    EXPECT_EQ(mov.AttachedPole(), nullptr);
}

TEST_F(PoleClimbingStateTest, CharacterControllerSkippedWhenClimbing)
{
    GameObject playerObj;
    auto& mov = *playerObj.AddComponent<CharacterMovementComponent>();
    mov.SetDebugDrawEnabled(false);

    GameObject poleObj;
    poleObj.Root().SetPosition({0.0f, 1.0f, 0.0f});
    auto& pole = *poleObj.AddComponent<PoleComponent>(0.2f, 2.0f);

    playerObj.Root().SetPosition({0.0f, 0.5f, 0.0f});

    PoleComponent* polePtrs[] = {&pole};
    mov.SetClimbables(std::span<PoleComponent* const>{polePtrs});

    mov.SetDesiredMove({1.0f, 0.0f, 0.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::ClimbingPole);

    // ClimbingPole 中は CharacterController を bypass しているため、 重力が velocity.y
    // を毎 frame 減らさず climb logic が velocity を完全に上書きする
    mov.SetDesiredMove({0.0f, 0.0f, 0.0f}, 0.0f);
    StepN(mov, 10);

    // gravity が累積していたら -25 m/s * 10 frames * dt = 数 m/s 単位の負値になるはず
    // ClimbingPole の bypass が効いていれば velocity.y は概ね 0 のまま
    EXPECT_LT(std::abs(mov.Velocity().y), 1.0f);
}
