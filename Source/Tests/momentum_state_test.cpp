#include <Game/Level/MomentumComponent.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Curve.h>
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
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr int k_PromoteSteps = 150; // 昇格秒 2.5 秒ぶんの固定ステップ数
    constexpr int k_GraceSteps = 30;    // 降格猶予秒 0.5 秒ぶんの固定ステップ数

    const Vector3 k_Forward{1.0f, 0.0f, 0.0f};
    const Vector3 k_Backward{-1.0f, 0.0f, 0.0f};

    // 2 点にしたのは、点を増やすと昇格歩数の期待値がカーブの補間に依存して読みにくくなるため
    [[nodiscard]] NS::Object::Curve RisingRateCurve()
    {
        NS::Object::Curve curve;
        curve.count = 2;
        curve.keys[0] = NS::Object::Curve::Key{0.0f, 1.0f};
        curve.keys[1] = NS::Object::Curve::Key{1.0f, 3.0f};
        return curve;
    }
} // namespace

class MomentumState : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);

        m_object.AddComponent<PlayerStateManagerComponent>();
        m_movement = m_object.AddComponent<PlayerComponent>();
        m_momentum = m_object.AddComponent<MomentumComponent>();

        // 床は走り切る長さだけ。広げるほど broadphase の格子が増え、 1 件で数分かかる
        m_world.AddAABB(AABB{Vector3{192.0f, -0.5f, 0.0f}, Vector3{224.0f, 0.5f, 4.0f}});
        m_world.BuildBroadphase();
        m_object.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
        m_movement->SetPhysicsWorld(&m_world);
        m_movement->SetDebugDrawEnabled(false);
        m_movement->OnStart();
        m_object.FindComponent<PlayerStateManagerComponent>()->OnStart();
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

    // 滞空中の 1 歩目と最終歩の猶予。空中で猶予が進んでいないかを両端の差で見る
    struct FlightResult
    {
        int airborneSteps = 0;
        float graceAtFirstAirborneStep = 0.0f;
        float graceAtLastAirborneStep = 0.0f;
    };

    // 走行入力を切って跳び、着地するまで回す。跳んだ後に入力を戻さないので接地中なら猶予が進む条件
    FlightResult JumpAndHold(int maxSteps)
    {
        FlightResult result;
        m_movement->SetJumpPressed();
        for (int i = 0; i < maxSteps; ++i)
        {
            m_movement->SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
            m_movement->SetJumpHeld(true);
            m_momentum->OnUpdate();
            m_movement->OnUpdate();

            if (!m_movement->IsGrounded())
            {
                if (result.airborneSteps == 0)
                    result.graceAtFirstAirborneStep = m_momentum->GraceSeconds();
                result.graceAtLastAirborneStep = m_momentum->GraceSeconds();
                ++result.airborneSteps;
            }
            else if (result.airborneSteps > 0)
                break;
        }
        return result;
    }

    void ReachMaxDash()
    {
        Run(k_PromoteSteps, 1.0f);
        ASSERT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    }

    // 移動を回さないのは、回すと着地の押し戻しで下向き速度が消えて勾配が作れないため。接地は SetUp の着地が残る
    void RunWithPinnedVelocity(const Vector3& velocity, int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            m_movement->SetDesiredMove(k_Forward, 1.0f);
            m_movement->SetVelocity(velocity);
            m_momentum->OnUpdate();
        }
    }

    // Inspector と同じリフレクション経路で書く。この欄だけの公開 setter を作らないため
    void SetPromoteRateCurve(const NS::Object::Curve& curve)
    {
        const NS::Object::FieldDesc* field =
            NS::Object::FindField(MomentumComponent::StaticReflection(), "昇格倍率カーブ");
        ASSERT_NE(field, nullptr);
        field->set(m_momentum, &curve);
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
    PlayerComponent* m_movement = nullptr;
    MomentumComponent* m_momentum = nullptr;
};

TEST_F(MomentumState, PromotesToMaxDashAfterFullThrottleRun)
{
    ASSERT_TRUE(m_movement->IsGrounded());
    Run(k_PromoteSteps, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, StaysNormalOneStepShortOfPromotion)
{
    ASSERT_TRUE(m_movement->IsGrounded());
    Run(k_PromoteSteps - 1, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);
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

    Run(k_PromoteSteps, 1.0f);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 16.0f);
}

TEST_F(MomentumState, NoThirdLevelBeyondMaxDash)
{
    Run(k_PromoteSteps, 1.0f);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);

    Run(600, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 16.0f);
}

TEST_F(MomentumState, KeepsMaxDashWhileAirborneBeyondGrace)
{
    ReachMaxDash();

    const FlightResult flight = JumpAndHold(600);
    ASSERT_GT(flight.airborneSteps, k_GraceSteps) << "滞空が猶予秒より短く、猶予を止めた効果が出ない跳び方になっている";

    EXPECT_FLOAT_EQ(flight.graceAtLastAirborneStep, flight.graceAtFirstAirborneStep);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, KeepsLevelOneStepShortOfGrace)
{
    ReachMaxDash();

    Release(k_GraceSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
    EXPECT_TRUE(m_momentum->IsInGrace());
    EXPECT_GT(m_momentum->GraceSeconds(), 0.0f);
}

TEST_F(MomentumState, DemotesToNormalWhenGraceExpires)
{
    ReachMaxDash();

    Release(k_GraceSteps);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);
    EXPECT_FLOAT_EQ(m_movement->MaxSpeed(), 8.0f);
}

TEST_F(MomentumState, DemotesToNormalAndStopsThere)
{
    ReachMaxDash();

    Release(k_GraceSteps);
    ASSERT_EQ(m_momentum->Level(), MomentumLevel::Normal);

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
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);
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

    Run(k_PromoteSteps, 1.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
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

// 反発から始まる猶予。走行入力を出したまま弾かれるので、入力が切れた時の猶予とは別の入口が要る
TEST_F(MomentumState, ReboundStartsGraceWhileRunInputHeld)
{
    ReachMaxDash();
    Run(1, 1.0f);
    ASSERT_FALSE(m_momentum->IsInGrace());

    m_momentum->BeginGrace();

    EXPECT_TRUE(m_momentum->IsInGrace());
}

TEST_F(MomentumState, ReboundGraceClearsOnFirstGroundedRunStep)
{
    ReachMaxDash();
    m_momentum->BeginGrace();
    ASSERT_TRUE(m_momentum->IsInGrace());

    Run(1, 1.0f);

    EXPECT_FALSE(m_momentum->IsInGrace());
    EXPECT_FLOAT_EQ(m_momentum->GraceSeconds(), 0.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

// 反発は上向きの初速を混ぜるので猶予は空中で始まる。接地していない間は解けず、秒だけ進む
TEST_F(MomentumState, ReboundGraceAdvancesWhileAirborne)
{
    ReachMaxDash();

    m_movement->SetJumpPressed();
    for (int i = 0; i < 30 && m_movement->IsGrounded(); ++i)
    {
        m_movement->SetJumpHeld(true);
        m_movement->OnUpdate();
    }
    ASSERT_FALSE(m_movement->IsGrounded());

    m_momentum->BeginGrace();
    ASSERT_FLOAT_EQ(m_momentum->GraceSeconds(), 0.0f);

    constexpr int k_AirSteps = 10;
    for (int i = 0; i < k_AirSteps; ++i)
    {
        m_movement->SetDesiredMove(k_Forward, 1.0f);
        m_movement->SetJumpHeld(true);
        m_momentum->OnUpdate();
        m_movement->OnUpdate();
        ASSERT_FALSE(m_movement->IsGrounded());
    }

    EXPECT_TRUE(m_momentum->IsInGrace());
    EXPECT_FLOAT_EQ(m_momentum->GraceSeconds(), k_FixedDt * static_cast<float>(k_AirSteps));
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, ReboundGraceDemotesWhenExpired)
{
    ReachMaxDash();
    m_momentum->BeginGrace();

    Release(k_GraceSteps);

    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);
    EXPECT_FALSE(m_momentum->IsInGrace());
}

// 速度と逆向きの入力は、厳しい側の設定では走り直したことにならない
TEST_F(MomentumState, ReboundGraceHoldsAgainstOpposedInputWhenForwardRequired)
{
    ReachMaxDash();
    SetRequireForwardInput(true);
    ASSERT_GT(m_movement->Velocity().x, 0.0f);

    m_momentum->BeginGrace();
    HoldMomentumOnly(k_Backward, k_GraceSteps - 1, 1.0f);

    EXPECT_TRUE(m_momentum->IsInGrace());
    EXPECT_GT(m_momentum->GraceSeconds(), 0.0f);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, DefaultCurveKeepsFlatPromotionStep)
{
    RunWithPinnedVelocity(Vector3{0.0f, 0.0f, 8.0f}, k_PromoteSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);

    RunWithPinnedVelocity(Vector3{0.0f, 0.0f, 8.0f}, 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, DefaultCurveKeepsPromotionStepOnDescent)
{
    RunWithPinnedVelocity(Vector3{0.0f, -8.0f, 8.0f}, k_PromoteSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);

    RunWithPinnedVelocity(Vector3{0.0f, -8.0f, 8.0f}, 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

// 50 歩ちょうどで境目を見るのは、倍率 3 が昇格歩数を 150 から 50 へ縮めるため
TEST_F(MomentumState, RisingCurvePromotesEarlierOnDescent)
{
    SetPromoteRateCurve(RisingRateCurve());

    RunWithPinnedVelocity(Vector3{0.0f, -8.0f, 8.0f}, k_PromoteSteps / 3 - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);

    RunWithPinnedVelocity(Vector3{0.0f, -8.0f, 8.0f}, 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

TEST_F(MomentumState, RisingCurveKeepsFlatPromotionStep)
{
    SetPromoteRateCurve(RisingRateCurve());

    RunWithPinnedVelocity(Vector3{0.0f, 0.0f, 8.0f}, k_PromoteSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);

    RunWithPinnedVelocity(Vector3{0.0f, 0.0f, 8.0f}, 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

// 点が無いと Evaluate が 0 を返し、そのまま倍率にすると昇格が永久に止まるため、1 とみなす側を見張る
TEST_F(MomentumState, EmptyCurveKeepsPromotionStep)
{
    SetPromoteRateCurve(NS::Object::Curve{});

    RunWithPinnedVelocity(Vector3{0.0f, 0.0f, 8.0f}, k_PromoteSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);

    RunWithPinnedVelocity(Vector3{0.0f, 0.0f, 8.0f}, 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}

// 負の勾配をカーブへ渡すと端の値へ張り付いて登りの昇格が変わり得る。登りで昇格歩数が平地と変わらないことを見る
TEST_F(MomentumState, RisingCurveKeepsPromotionStepOnAscent)
{
    SetPromoteRateCurve(RisingRateCurve());

    RunWithPinnedVelocity(Vector3{0.0f, 8.0f, 8.0f}, k_PromoteSteps - 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::Normal);

    RunWithPinnedVelocity(Vector3{0.0f, 8.0f, 8.0f}, 1);
    EXPECT_EQ(m_momentum->Level(), MomentumLevel::MaxDash);
}
