#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Scene/CharacterMovementComponent.h>
#include <Framework/Scene/ClimbableSurfaceComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/PoleComponent.h>
#include <Framework/Scene/Transform.h>

#include <span>

namespace
{
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::ClimbableKind;
    using NS::Scene::ClimbableSurfaceComponent;
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

class ClimbStateTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kFixedDt); }
};

TEST_F(ClimbStateTest, FenceGrabOnContact)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    GameObject fenceObj;
    fenceObj.Root().SetPosition({0.0f, 1.0f, 0.0f});
    ClimbableSurfaceComponent fence(
        &fenceObj, ClimbableKind::Fence, NS::Core::Vector3{0.5f, 0.5f, 0.1f}, NS::Core::Vector3{0.0f, 0.0f, -1.0f});

    playerObj.Root().SetPosition({0.0f, 1.0f, 0.0f});

    ClimbableSurfaceComponent* fencePtrs[] = {&fence};
    std::span<PoleComponent* const> emptyPoles{};
    mov.SetClimbables(std::span<ClimbableSurfaceComponent* const>{fencePtrs}, emptyPoles);

    mov.SetDesiredMove({0.0f, 0.0f, 1.0f}, 1.0f);
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::ClimbingFence);
    EXPECT_EQ(mov.AttachedFence(), &fence);
}

TEST_F(ClimbStateTest, FenceExitOnJump)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    GameObject fenceObj;
    fenceObj.Root().SetPosition({0.0f, 1.0f, 0.0f});
    ClimbableSurfaceComponent fence(
        &fenceObj, ClimbableKind::Fence, NS::Core::Vector3{0.5f, 0.5f, 0.1f}, NS::Core::Vector3{0.0f, 0.0f, -1.0f});

    playerObj.Root().SetPosition({0.0f, 1.0f, 0.0f});

    ClimbableSurfaceComponent* fencePtrs[] = {&fence};
    std::span<PoleComponent* const> emptyPoles{};
    mov.SetClimbables(std::span<ClimbableSurfaceComponent* const>{fencePtrs}, emptyPoles);

    mov.SetDesiredMove({0.0f, 0.0f, 1.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::ClimbingFence);

    mov.SetJumpPressed();
    StepN(mov, 1);

    EXPECT_EQ(mov.State(), MovementState::Falling);
    EXPECT_EQ(mov.AttachedFence(), nullptr);
}

TEST_F(ClimbStateTest, FenceExitOnReachingTop)
{
    GameObject playerObj;
    CharacterMovementComponent mov(&playerObj);
    mov.SetDebugDrawEnabled(false);

    GameObject fenceObj;
    fenceObj.Root().SetPosition({0.0f, 1.0f, 0.0f});
    ClimbableSurfaceComponent fence(
        &fenceObj, ClimbableKind::Fence, NS::Core::Vector3{0.5f, 0.5f, 0.1f}, NS::Core::Vector3{0.0f, 0.0f, -1.0f});

    playerObj.Root().SetPosition({0.0f, 1.0f, 0.0f});

    ClimbableSurfaceComponent* fencePtrs[] = {&fence};
    std::span<PoleComponent* const> emptyPoles{};
    mov.SetClimbables(std::span<ClimbableSurfaceComponent* const>{fencePtrs}, emptyPoles);

    mov.SetDesiredMove({0.0f, 0.0f, 1.0f}, 1.0f);
    StepN(mov, 1);
    ASSERT_EQ(mov.State(), MovementState::ClimbingFence);

    // Fence top (y = 1.5) を超えるまで上方向入力を継続。 60 frame で 1m 弱登る climb 速度
    // (≈ 1.5 m/s) を仮定し、 余裕を持って 120 frame 進めれば必ず top に達する。
    mov.SetDesiredMove({0.0f, 1.0f, 0.0f}, 1.0f);
    for (int i = 0; i < 120; ++i)
    {
        mov.OnUpdate();
        if (mov.State() == MovementState::Walking)
            break;
    }

    EXPECT_EQ(mov.State(), MovementState::Walking);
}
