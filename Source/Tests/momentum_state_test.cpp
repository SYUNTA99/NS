#include <Game/Level/MomentumComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Level::MomentumComponent;
    using NS::Game::Level::MomentumLevel;
    using NS::Object::CharacterMovementComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr int k_DashSteps = 90;     // ダッシュ昇格秒 1.5 秒ぶんの固定ステップ数
    constexpr int k_MaxDashSteps = 150; // 最高ダッシュ昇格秒 2.5 秒ぶんの固定ステップ数
    constexpr int k_GraceSteps = 30;    // 降格猶予秒 0.5 秒ぶんの固定ステップ数

    const Vector3 k_Forward{1.0f, 0.0f, 0.0f};
    const Vector3 k_Backward{-1.0f, 0.0f, 0.0f};
} // namespace

class MomentumState : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);

        m_movement = m_object.AddComponent<CharacterMovementComponent>();
        m_momentum = m_object.AddComponent<MomentumComponent>();

        // 床は走り切る長さだけ。広げるほど broadphase の格子が増え、 1 件で数分かかる
        m_world.AddAABB(AABB{Vector3{192.0f, -0.5f, 0.0f}, Vector3{224.0f, 0.5f, 4.0f}});
        m_world.BuildBroadphase();
        m_object.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
        m_movement->SetPhysicsWorld(&m_world);
        m_movement->SetDebugDrawEnabled(false);
        m_momentum->OnStart();

        for (int i = 0; i < 30 && !m_movement->IsGrounded(); ++i)
            m_movement->OnUpdate();
    }

    // speedScale が 0 の時は入力を渡さない。走行入力なしで段が上がらないことの検証に使う
    void Run(int steps, float speedScale)
    {
        for (int i = 0; i < steps; ++i)
        {
            // 呼ぶ順は帯の並びと同じ。MomentumComponent が先で、決めた最高速度をその歩の移動が使う
            if (speedScale > 0.0f)
                m_movement->SetDesiredMove(k_Forward, speedScale);
            m_momentum->OnUpdate();
            m_movement->OnUpdate();
        }
    }

    // 向きを指定して回す。復帰の判定が速度と入力の向きを見る設定を試すのに使う
    void RunToward(const Vector3& direction, int steps, float speedScale)
    {
        for (int i = 0; i < steps; ++i)
        {
            m_movement->SetDesiredMove(direction, speedScale);
            m_momentum->OnUpdate();
            m_movement->OnUpdate();
        }
    }

    // 走行入力を明示的に切って回す。SetDesiredMove を呼ばないだけでは前の歩の入力が残る
    void Release(int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            m_movement->SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
            m_momentum->OnUpdate();
            m_movement->OnUpdate();
        }
    }

    // 移動を回さずに段だけ進める。速度の向きを固定したまま入力の向きだけを変えて内積の側を見る
    // 移動も回すと 5 歩ほどで速度が入力側へ向き、内積が正へ戻って猶予が 0 に戻る
    void HoldMomentumOnly(const Vector3& direction, int steps, float speedScale)
    {
        for (int i = 0; i < steps; ++i)
        {
            m_movement->SetDesiredMove(direction, speedScale);
            m_momentum->OnUpdate();
        }
    }

    void ReachMaxDash()
    {
        Run(k_DashSteps + k_MaxDashSteps, 1.0f);
        ASSERT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    }

    // Inspector と同じリフレクション経路で欄を書き換える。公開の setter を作らずに切り替えを試す
    void SetRequireForwardInput(bool require)
    {
        const NS::Object::FieldDesc* field =
            NS::Object::FindField(MomentumComponent::StaticReflection(), "復帰に進行方向入力を要求");
        ASSERT_NE(field, nullptr);
        field->set(m_momentum, &require);
    }

    GameObject m_object;
    NS::Physics::PhysicsWorld m_world;
    CharacterMovementComponent* m_movement = nullptr;
    MomentumComponent* m_momentum = nullptr;
};

TEST_F(MomentumState, PromotesToDashAfterFullThrottleRun)
{
    ASSERT_TRUE(m_movement->IsGrounded());
    Run(k_DashSteps, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Dash);
}

TEST_F(MomentumState, StaysNormalOneStepShortOfPromotion)
{
    ASSERT_TRUE(m_movement->IsGrounded());
    Run(k_DashSteps - 1, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);
}

TEST_F(MomentumState, PromotesToMaxDashAfterSecondRun)
{
    Run(k_DashSteps, 1.0f);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::Dash);
    Run(k_MaxDashSteps, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, NeverPromotesWithoutRunInput)
{
    Run(300, 0.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);
}

TEST_F(MomentumState, MaxSpeedFollowsLevel)
{
    Run(1, 0.0f);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 8.0f);

    Run(k_DashSteps, 1.0f);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::Dash);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 12.0f);

    Run(k_MaxDashSteps, 1.0f);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 16.0f);
}

TEST_F(MomentumState, NoFourthLevelBeyondMaxDash)
{
    Run(k_DashSteps + k_MaxDashSteps, 1.0f);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);

    Run(600, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 16.0f);
}

TEST_F(MomentumState, KeepsLevelOneStepShortOfGrace)
{
    ReachMaxDash();

    Release(k_GraceSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    EXPECT_TRUE(m_momentum->IsInGrace());
    EXPECT_GT(m_momentum->GraceSeconds(), 0.0f);
}

TEST_F(MomentumState, DemotesOneLevelWhenGraceExpires)
{
    ReachMaxDash();

    Release(k_GraceSteps);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Dash);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 12.0f);
}

TEST_F(MomentumState, DemotesToNormalAndStopsThere)
{
    ReachMaxDash();

    Release(k_GraceSteps);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::Dash);

    Release(k_GraceSteps);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);

    Release(300);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);
    EXPECT_FALSE(m_momentum->IsInGrace());
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 8.0f);
}

TEST_F(MomentumState, RunInputWithinGraceKeepsLevel)
{
    ReachMaxDash();

    Release(k_GraceSteps - 1);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);

    Run(1, 1.0f);
    EXPECT_FLOAT_EQ(m_momentum->GraceSeconds(), 0.0f);
    EXPECT_FALSE(m_momentum->IsInGrace());

    Release(k_GraceSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, OpposedInputStartsGraceWhenForwardRequired)
{
    ReachMaxDash();
    SetRequireForwardInput(true);
    ASSERT_GT(m_movement->Velocity().x, 0.0f);

    RunToward(k_Backward, 1, 1.0f);
    EXPECT_TRUE(m_momentum->IsInGrace());
    EXPECT_GT(m_momentum->GraceSeconds(), 0.0f);
}

TEST_F(MomentumState, OpposedInputDemotesWhenForwardRequired)
{
    ReachMaxDash();
    SetRequireForwardInput(true);
    ASSERT_GT(m_movement->Velocity().x, 0.0f);

    HoldMomentumOnly(k_Backward, k_GraceSteps, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Dash);
}

TEST_F(MomentumState, ForwardInputClearsGraceWhenForwardRequired)
{
    ReachMaxDash();
    SetRequireForwardInput(true);

    RunToward(k_Backward, 1, 1.0f);
    ASSERT_TRUE(m_momentum->IsInGrace());

    RunToward(k_Forward, 1, 1.0f);
    EXPECT_FLOAT_EQ(m_momentum->GraceSeconds(), 0.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

// 厳しい側の設定で立ち止まりから走り出せるか。速度が 0 の間は比べる向きが無い
TEST_F(MomentumState, PromotesFromStandstillWhenForwardRequired)
{
    SetRequireForwardInput(true);
    ASSERT_FLOAT_EQ(m_movement->Velocity().x, 0.0f);
    ASSERT_FLOAT_EQ(m_movement->Velocity().z, 0.0f);

    Run(k_DashSteps, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Dash);
}

TEST_F(MomentumState, OpposedInputKeepsLevelWhenForwardNotRequired)
{
    ReachMaxDash();
    ASSERT_GT(m_movement->Velocity().x, 0.0f);

    // 1 歩目で見る。30 歩後だと速度が入力側へ向き直って猶予が 0 へ戻り、設定の違いが消える
    RunToward(k_Backward, 1, 1.0f);
    EXPECT_FLOAT_EQ(m_momentum->GraceSeconds(), 0.0f);
    EXPECT_FALSE(m_momentum->IsInGrace());

    RunToward(k_Backward, k_GraceSteps, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}
