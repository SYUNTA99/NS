#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Math/Math.h>
#include <Framework/Scene/Components/CharacterMovementComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

#include <span>
#include <vector>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Vector3;
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;

    constexpr float kFixedDt = 1.0f / 60.0f;
    constexpr int kNumSteps = 120;
    constexpr int kJumpPressStep = 10;
    constexpr int kJumpReleaseStep = 20;
    constexpr float kDeterministicEpsilon = 1.0e-5f;

    /// 床 1 枚 (center y=-0.5, half y=0.5、 上面が y=0) のみ。 壁なし
    AABB MakeFloorOnly() noexcept
    {
        return AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{8.0f, 0.5f, 8.0f}};
    }

    /// 入力 schedule を 2 run で完全一致させ、 trajectory を返す
    /// X+ 方向に max speed walk しつつ kJumpPressStep で jump press、
    /// kJumpReleaseStep で release (10 step held = variable height 中位)
    std::vector<Vector3> RunDeterministicSim()
    {
        const AABB floor = MakeFloorOnly();

        GameObject owner;
        auto& movement = *owner.AddComponent<CharacterMovementComponent>();
        owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});

        movement.SetCollisionWorld(std::span<const AABB>(&floor, 1));
        movement.SetDebugDrawEnabled(false);

        std::vector<Vector3> trajectory;
        trajectory.reserve(kNumSteps);

        for (int i = 0; i < kNumSteps; ++i)
        {
            movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            if (i == kJumpPressStep)
                movement.SetJumpPressed();
            movement.SetJumpHeld(i >= kJumpPressStep && i < kJumpReleaseStep);

            movement.OnUpdate();
            trajectory.push_back(owner.Root().Position());
        }
        return trajectory;
    }
} // namespace

class MovementIntegrity : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kFixedDt); }
};

/// SC5 機械的証明の核: fixed-step 物理が DeltaTime/random 等の non-deterministic
/// 入力を一切使っていないなら、 同条件 2 run の trajectory は bit-exact 一致する
/// std::exp 使用のため EXPECT_NEAR (1e-5) で誤差耐性を持たせる
TEST_F(MovementIntegrity, JumpTrajectoryIsDeterministicAcrossTwoRuns)
{
    const auto traj1 = RunDeterministicSim();
    const auto traj2 = RunDeterministicSim();

    ASSERT_EQ(traj1.size(), traj2.size());
    ASSERT_EQ(traj1.size(), static_cast<size_t>(kNumSteps));

    for (size_t i = 0; i < traj1.size(); ++i)
    {
        EXPECT_NEAR(traj1[i].x, traj2[i].x, kDeterministicEpsilon) << "step " << i << " x";
        EXPECT_NEAR(traj1[i].y, traj2[i].y, kDeterministicEpsilon) << "step " << i << " y";
        EXPECT_NEAR(traj1[i].z, traj2[i].z, kDeterministicEpsilon) << "step " << i << " z";
    }
}

/// 解析的に予想される peak height (jumpImpulse=12, gravity ~25-35) の妥当 range に
/// 入ることを sanity check。 asymmetric gravity + apex hang + jumpReleaseScale 込みで
/// 厳密値は出ないため広め range で
TEST_F(MovementIntegrity, JumpReachesExpectedPeakHeightRange)
{
    const auto trajectory = RunDeterministicSim();

    float peakY = -1000.0f;
    for (const Vector3& p : trajectory)
    {
        if (p.y > peakY)
            peakY = p.y;
    }

    EXPECT_GT(peakY, 2.0f) << "jump peak too low (initial y=1.0)";
    EXPECT_LT(peakY, 6.0f) << "jump peak unreasonably high";
}

/// walkTau = 0.10s で 1 秒間 (60 step) 走れば maxSpeed=8 にほぼ到達する
/// ただし jump 中は asymmetric gravity / 着地 substep が x velocity を一時変動
/// させる可能性があるため、 jump 前 (step 9 まで) の Vx 単純比較で sanity check
TEST_F(MovementIntegrity, WalkVelocityApproachesMaxSpeedBeforeJump)
{
    const AABB floor = MakeFloorOnly();

    GameObject owner;
    auto& movement = *owner.AddComponent<CharacterMovementComponent>();
    owner.Root().SetPosition(Vector3{0.0f, 0.5f, 0.0f}); // 床上に直置きで grounded スタート

    movement.SetCollisionWorld(std::span<const AABB>(&floor, 1));
    movement.SetDebugDrawEnabled(false);

    for (int i = 0; i < 60; ++i)
    {
        movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
        movement.OnUpdate();
    }

    const float vx = movement.Velocity().x;
    EXPECT_GT(vx, 6.0f) << "walk velocity should approach maxSpeed (8) after 1 sec";
    EXPECT_LT(vx, 9.0f) << "walk velocity should not exceed maxSpeed (8) significantly";
}
