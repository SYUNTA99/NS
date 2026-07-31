#include <gtest/gtest.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <vector>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Vector3;
    using NS::Object::CharacterMovementComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr int k_NumSteps = 120;
    constexpr int k_JumpPressStep = 10;
    constexpr int k_JumpReleaseStep = 20;
    constexpr float k_DeterministicEpsilon = 1.0e-5f;

    /// 床 1 枚 (center y=-0.5, half y=0.5、 上面が y=0) のみ。 壁なし
    AABB MakeFloorOnly() noexcept
    {
        return AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{8.0f, 0.5f, 8.0f}};
    }

    /// 毎回同じ入力列で走らせて軌跡を返す
    /// X+ へ全開で歩く。k_JumpPressStep でジャンプを押して k_JumpReleaseStep で離す (保持 10 step)
    std::vector<Vector3> RunDeterministicSim()
    {
        const AABB floor = MakeFloorOnly();

        GameObject owner;
        auto& movement = *owner.AddComponent<CharacterMovementComponent>();
        owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});

        NS::Physics::PhysicsWorld pw;
        pw.AddAABB(floor);
        pw.BuildBroadphase();
        movement.SetPhysicsWorld(&pw);
        movement.SetDebugDrawEnabled(false);

        std::vector<Vector3> trajectory;
        trajectory.reserve(k_NumSteps);

        for (int i = 0; i < k_NumSteps; ++i)
        {
            movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            if (i == k_JumpPressStep)
                movement.SetJumpPressed();
            movement.SetJumpHeld(i >= k_JumpPressStep && i < k_JumpReleaseStep);

            movement.OnUpdate();
            trajectory.push_back(owner.Root().Position());
        }
        return trajectory;
    }
} // namespace

class MovementIntegrity : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

/// 固定ステップ物理が DeltaTime や乱数を使っていなければ、同条件で 2 回走らせた軌跡は一致する
/// std::exp を使うので EXPECT_NEAR (1e-5) で誤差を許す
TEST_F(MovementIntegrity, JumpTrajectoryIsDeterministicAcrossTwoRuns)
{
    const auto traj1 = RunDeterministicSim();
    const auto traj2 = RunDeterministicSim();

    ASSERT_EQ(traj1.size(), traj2.size());
    ASSERT_EQ(traj1.size(), static_cast<size_t>(k_NumSteps));

    for (size_t i = 0; i < traj1.size(); ++i)
    {
        EXPECT_NEAR(traj1[i].x, traj2[i].x, k_DeterministicEpsilon) << "step " << i << " x";
        EXPECT_NEAR(traj1[i].y, traj2[i].y, k_DeterministicEpsilon) << "step " << i << " y";
        EXPECT_NEAR(traj1[i].z, traj2[i].z, k_DeterministicEpsilon) << "step " << i << " z";
    }
}

/// ジャンプ頂点が妥当な高さ (jumpImpulse=12, gravity 25〜35) に収まること
/// 非対称重力や頂点の重力緩和、jumpReleaseScale が絡んで厳密値は出ないので範囲は広めにとる
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

/// walkTau = 0.10s なら 1 秒 (60 step) でほぼ maxSpeed=8 に届く
/// ジャンプ中は非対称重力や着地の substep で x 速度が揺れるので、ジャンプ無しの歩行だけで見る
TEST_F(MovementIntegrity, WalkVelocityApproachesMaxSpeedBeforeJump)
{
    const AABB floor = MakeFloorOnly();

    GameObject owner;
    auto& movement = *owner.AddComponent<CharacterMovementComponent>();
    owner.Root().SetPosition(Vector3{0.0f, 0.5f, 0.0f}); // 床の上に直置きして接地から始める

    NS::Physics::PhysicsWorld pw;
    pw.AddAABB(floor);
    pw.BuildBroadphase();
    movement.SetPhysicsWorld(&pw);
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
