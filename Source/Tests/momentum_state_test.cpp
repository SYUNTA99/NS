#include <Game/Level/MomentumComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/GameObject.h>
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
                m_movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, speedScale);
            m_momentum->OnUpdate();
            m_movement->OnUpdate();
        }
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
