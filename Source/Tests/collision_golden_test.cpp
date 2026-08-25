#include "golden_trace.h"

#include "Game/Level/BlockObject.h"
#include "Game/Player.h"

#include <Game/Level/BreakableComponent.h>
#include <Game/Level/ImpactResolverComponent.h>
#include <Game/Level/MomentumComponent.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/PlayerInputComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Object/World.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Level::MomentumComponent;
    using NS::Game::Level::MomentumLevel;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManagerComponent;
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
            m_object.AddComponent<PlayerStateManagerComponent>();
            m_movement = m_object.AddComponent<PlayerComponent>();
            m_momentum = m_object.AddComponent<MomentumComponent>();

            // 床は走り切る z 方向だけ伸ばす。全方向へ広げると broadphase の格子が膨らみ 1 件で数分かかる
            m_world.AddAABB(AABB{Vector3{0.0f, -0.5f, 32.0f}, Vector3{4.0f, 0.5f, 44.0f}});
            m_world.BuildBroadphase();
            m_object.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
            m_movement->SetPhysicsWorld(&m_world);
            m_movement->SetDebugDrawEnabled(false);
            m_movement->OnStart();
            m_object.FindComponent<PlayerStateManagerComponent>()->OnStart();
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
        PlayerComponent* m_movement = nullptr;
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

    constexpr int k_ImpactSteps = 360; // 6 秒。押し飛ばした物へ追いついて当て直す往復が 4 回入る長さ
    // 突進 0.3 秒と凍結と反発からの立て直しが 1 周に収まる間隔。短いと接地待ちで発動が落ちて経路が読めない
    constexpr int k_ImpactSlamPeriod = 30;
    constexpr float k_ImpactSlamCharge = 1.0f;
    // 最高ダッシュ 16.0 で 6 秒ぶん走り切れる長さ。短いと道の端から落ち、軌跡の大半が自由落下になる
    constexpr std::int16_t k_ImpactFloorCells = 100;
    constexpr std::int16_t k_ImpactTargetZ = 6;
    constexpr float k_ImpactTargetMass = 1.0f;
    // 壊れない高さ。破壊が入っても反発と押し飛ばしの経路が変わらない
    constexpr float k_ImpactTargetToughness = 99.0f;

    // 反発と押し飛ばしを含む経路。当たりを持つ配置物が要るので Scene を組む
    class ImpactRig
    {
    public:
        ImpactRig()
        {
            NS::Object::SceneData data;
            for (std::int16_t z = 0; z < k_ImpactFloorCells; ++z)
                data.objects.push_back(NS::Game::Level::MakeCellObject(0, 0, z));

            NS::Object::ObjectData player =
                MakePlayerObject(Vector3{0.0f, Player::k_DefaultSpawnY, 0.0f}, NS::Core::Quaternion{});
            player.components.push_back(NS::Object::MakeComponentEntry("MomentumComponent"));
            player.components.push_back(NS::Object::MakeComponentEntry("ImpactResolverComponent"));
            player.components.push_back(NS::Object::MakeComponentEntry("CollisionInputComponent"));
            data.objects.push_back(player);

            NS::Object::ObjectData target = NS::Game::Level::MakeCellObject(0, 1, k_ImpactTargetZ);
            target.components.push_back(NS::Object::MakeComponentEntry("BreakableComponent"));
            data.objects.push_back(target);

            m_scene.LoadFromData(std::move(data));

            Player* live = FindPlayer(m_scene.World());
            EXPECT_NE(live, nullptr);
            if (live != nullptr)
            {
                m_player = live;
                m_movement = live->FindComponent<PlayerComponent>();
                // 入力の component は EarlyUpdate で実機の入力を書き込む。起こしたままだと走行入力が毎歩 0 になる
                if (auto* input = live->FindComponent<NS::Object::PlayerInputComponent>())
                    input->SetActive(false);
            }
            m_scene.World().ForEachComponent<NS::Game::Level::BreakableComponent>(
                [](NS::Game::Level::BreakableComponent& breakable) {
                    breakable.SetMass(k_ImpactTargetMass);
                    breakable.SetToughness(k_ImpactTargetToughness);
                });
        }

        void Step(const Vector3& direction, float speedScale, int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                m_movement->SetDesiredMove(direction, speedScale);
                if (m_stepIndex % k_ImpactSlamPeriod == 0)
                    m_movement->RequestBodySlam(k_ImpactSlamCharge);
                ++m_stepIndex;
                m_scene.World().UpdateAllObjects();
                m_trace.push_back(
                    StepRecord{m_player->Root().Position(), m_movement->Velocity(), m_movement->IsGrounded()});
            }
        }

        [[nodiscard]] const std::vector<StepRecord>& Trace() const noexcept { return m_trace; }

    private:
        NS::Object::Scene m_scene;
        Player* m_player = nullptr;
        PlayerComponent* m_movement = nullptr;
        std::vector<StepRecord> m_trace;
        int m_stepIndex = 0;
    };

    // 壊せる物へ走り込み、反発しながら追いかけ直す。記録するのは自機だけで、飛ばされた物の位置は入れない
    std::vector<StepRecord> RunImpact()
    {
        ImpactRig rig;
        rig.Step(k_Forward, 1.0f, k_ImpactSteps);
        return rig.Trace();
    }

    // 進行方向と逆へ弾かれた歩があるか。反発が消えた改修を軌跡の一致より先に知らせる
    bool HasReboundStep(const std::vector<StepRecord>& trace) noexcept
    {
        for (const StepRecord& s : trace)
        {
            if (s.velocity.z < -1.0f)
                return true;
        }
        return false;
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
    // 衝突: 質量 1.0 / 耐久 99.0 の壊せる物へ +Z へ速度スケール 1.0 で 360 固定ステップ
    // 耐久を高くして、破壊が入っても反発と押し飛ばしの経路が変わらないようにしてある
    // 反発を勢いと質量から作る式とヒットストップを入れた時に取り直した。取り直し前は 0x32375285390311FF
    // 凍結を 1 歩遅らせて触れてから止まる構図にした時に取り直した。取り直し前は 0x9104E912BEA391C4
    // 体当たりを発動しない歩は裁かない仕様にした時に取り直した。取り直し前は 0x9557D5FB66D8EAB9
    // 威力の比に下限を入れた時に取り直した。取り直し前は 0xBBEEE91DFF9BE37B
    // 体当たりの空中発動と先行入力を入れた時に取り直した。取り直し前は 0x226FEFA1B6B49164
    // 押し飛ばし基準初速を 14.0 から 20.0 へ上げた時に取り直した。取り直し前は 0x5CB7A5CC3E014443
    // 最初の衝突と反発を含む 30 歩は一致し、遠くへ飛んだ岩へ追いつく歩から差が出る
    // 基準初速を 32.0 へ、質量指数を 0.35 へ上げた時に取り直した。取り直し前は 0xA143A23EBE09F88A
    // 威力の比を発動時の実速度から勢いの段へ変えた時に取り直した。取り直し前は 0x6D19639767D9BB26
    // 走っているだけの歩は一致し、加速しきる前に出した最初の体当たりの歩から差が出る
    // 突進距離を 6.0 から 8.0 へ伸ばした時に取り直した。取り直し前は 0x47DD9DADAAD3A0A6
    // 突進距離を 10.0 へ伸ばした時に取り直した。取り直し前は 0x19AF2207089F933C
    constexpr std::uint64_t k_ImpactGolden = 0x48D67D77254C7DA6ULL;
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
    EXPECT_EQ(FoldTrace(RunImpact()), FoldTrace(RunImpact()));
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

TEST_F(CollisionGolden, ImpactMatchesGoldenTrace)
{
    const std::vector<StepRecord> trace = RunImpact();

    EXPECT_TRUE(HasReboundStep(trace)) << "経路に反発が現れていない";

    const std::uint64_t hash = FoldTrace(trace);
    EXPECT_EQ(hash, k_ImpactGolden) << DescribeTrace(trace, hash);
}
