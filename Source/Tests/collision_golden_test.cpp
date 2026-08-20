#include "golden_trace.h"

#include <Game/Level/MomentumComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Level::MomentumComponent;
    using NS::Game::Level::MomentumLevel;
    using NS::Object::CharacterMovementComponent;
    using NS::Object::GameObject;
    using NS::Tests::DescribeTrace;
    using NS::Tests::FoldTrace;
    using NS::Tests::StepRecord;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr int k_PromoteSteps = 300; // 5 秒。昇格が 2 回起きる長さ
    constexpr int k_ReleaseSteps = 120; // 2 秒。降格が 2 回起きる長さ
    constexpr int k_ReaccelSteps = 60;  // 1 秒。降格後の最高速度まで伸び切る長さ

    const Vector3 k_Forward{0.0f, 0.0f, 1.0f};
    const Vector3 k_NoInput{0.0f, 0.0f, 0.0f};

    // プレイヤー相当 1 体と床 1 枚だけの検証台。呼ぶ順は帯の並びと同じで MomentumComponent が先
    class Rig
    {
    public:
        Rig()
        {
            m_movement = m_object.AddComponent<CharacterMovementComponent>();
            m_momentum = m_object.AddComponent<MomentumComponent>();

            // 床は走り切る z 方向だけ伸ばす。全方向へ広げると broadphase の格子が膨らみ 1 件で数分かかる
            m_world.AddAABB(AABB{Vector3{0.0f, -0.5f, 32.0f}, Vector3{4.0f, 0.5f, 44.0f}});
            m_world.BuildBroadphase();
            m_object.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
            m_movement->SetPhysicsWorld(&m_world);
            m_movement->SetDebugDrawEnabled(false);
            m_momentum->OnStart();

            // 開始位置は空中に取る。床へ直置きするとカプセルがめり込み、衝突解決が移動を丸ごと拒否する
            for (int i = 0; i < 30 && !m_movement->IsGrounded(); ++i)
                m_movement->OnUpdate();
        }

        void Step(const Vector3& direction, float speedScale, int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                m_movement->SetDesiredMove(direction, speedScale);
                m_momentum->OnUpdate();
                m_movement->OnUpdate();
                m_trace.push_back(
                    StepRecord{m_object.Root().Position(), m_movement->Velocity(), m_movement->IsGrounded()});
            }
        }

        [[nodiscard]] const std::vector<StepRecord>& Trace() const noexcept { return m_trace; }
        [[nodiscard]] MomentumLevel Level() const noexcept { return m_momentum->Level(); }

    private:
        GameObject m_object;
        NS::Physics::PhysicsWorld m_world;
        CharacterMovementComponent* m_movement = nullptr;
        MomentumComponent* m_momentum = nullptr;
        std::vector<StepRecord> m_trace;
    };

    // 全開走行だけ。通常 -> ダッシュ -> 最高ダッシュ の 2 回の昇格を通る
    std::vector<StepRecord> RunPromote()
    {
        Rig rig;
        rig.Step(k_Forward, 1.0f, k_PromoteSteps);
        return rig.Trace();
    }

    // 全開走行の後に入力を切り、最後にもう一度走り直す
    // 走り直しが無いと軌跡に降格が現れない。入力を切っている間は速度が 0 へ落ちるだけで最高速度に触れない
    std::vector<StepRecord> RunDemote()
    {
        Rig rig;
        rig.Step(k_Forward, 1.0f, k_PromoteSteps);
        rig.Step(k_NoInput, 0.0f, k_ReleaseSteps);
        rig.Step(k_Forward, 1.0f, k_ReaccelSteps);
        return rig.Trace();
    }

    float MaxForwardSpeedFrom(const std::vector<StepRecord>& trace, std::size_t first) noexcept
    {
        float peak = 0.0f;
        for (std::size_t i = first; i < trace.size(); ++i)
            peak = std::max(trace[i].velocity.z, peak);
        return peak;
    }

    // 基準ハッシュ。意図して手触りを変えた時だけ実測値で更新する
    // 昇格: +Z へ速度スケール 1.0 で 300 固定ステップ
    constexpr std::uint64_t k_PromoteGolden = 0x15C1DFDE28E0F4D3ULL;
    // 降格: 上と同じ 300 固定ステップ -> 入力なしで 120 -> +Z へ速度スケール 1.0 で 60
    constexpr std::uint64_t k_DemoteGolden = 0x2D530BB91AB33A80ULL;
} // namespace

class CollisionGolden : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

TEST_F(CollisionGolden, HashIsStableAcrossTwoRuns)
{
    EXPECT_EQ(FoldTrace(RunPromote()), FoldTrace(RunPromote()));
    EXPECT_EQ(FoldTrace(RunDemote()), FoldTrace(RunDemote()));
}

TEST_F(CollisionGolden, PromoteMatchesGoldenTrace)
{
    Rig rig;
    rig.Step(k_Forward, 1.0f, k_PromoteSteps);
    ASSERT_EQ(rig.Level(), MomentumLevel::MaxDash);

    const std::vector<StepRecord>& trace = rig.Trace();
    EXPECT_GT(MaxForwardSpeedFrom(trace, 0), 15.0f) << "最高ダッシュ速度 16 まで伸びていない";
    EXPECT_TRUE(trace.back().grounded) << "走り切る前に床から外れている";

    const std::uint64_t hash = FoldTrace(trace);
    EXPECT_EQ(hash, k_PromoteGolden) << DescribeTrace(trace, hash);
}

TEST_F(CollisionGolden, DemoteMatchesGoldenTrace)
{
    Rig rig;
    rig.Step(k_Forward, 1.0f, k_PromoteSteps);
    ASSERT_EQ(rig.Level(), MomentumLevel::MaxDash);

    rig.Step(k_NoInput, 0.0f, k_ReleaseSteps);
    ASSERT_EQ(rig.Level(), MomentumLevel::Normal);

    rig.Step(k_Forward, 1.0f, k_ReaccelSteps);

    const std::vector<StepRecord>& trace = rig.Trace();
    const std::size_t reaccelFirst = static_cast<std::size_t>(k_PromoteSteps + k_ReleaseSteps);
    EXPECT_LT(MaxForwardSpeedFrom(trace, reaccelFirst), 9.0f) << "降格したのに通常速度 8 を超えて走り直している";
    EXPECT_TRUE(trace.back().grounded) << "走り切る前に床から外れている";

    const std::uint64_t hash = FoldTrace(trace);
    EXPECT_EQ(hash, k_DemoteGolden) << DescribeTrace(trace, hash);
}
