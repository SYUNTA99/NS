#include <Game/Entity/EntityStateManagerComponent.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStatsManagerComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CameraBrainComponent.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStatsManagerComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    //! 現在状態の名前を覚えて答えるだけの状態管理
    //! @details 本物の StateMachine を積むと登録名 "BodySlam" / "Idle" が本番の状態とぶつかる。
    //! 状態機械そのものは entity_state_manager_test が見張る
    class NamedStateManager final : public NS::Game::Entity::EntityStateManagerComponent
    {
    public:
        [[nodiscard]] const char* CurrentName() const noexcept override { return m_current.c_str(); }
        [[nodiscard]] bool IsBuilt() const noexcept override { return true; }
        bool ChangeByName(std::string_view name) override
        {
            m_current.assign(name);
            return true;
        }
        void ResetToFirst() noexcept override { m_current = PlayerComponent::k_IdleStateName; }

    private:
        std::string m_current = PlayerComponent::k_IdleStateName;
    };

    //! 床 1 枚を敷いて接地させた自機を返す。壁は呼び出し側が先に足す
    //! @details 状態管理を先に積むのは OnStart が同居から引き当てるため
    PlayerComponent& MakeSlamReady(GameObject& owner, NS::Physics::PhysicsWorld& world)
    {
        owner.AddComponent<NamedStateManager>();
        auto& player = *owner.AddComponent<PlayerComponent>();

        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{64.0f, 0.5f, 64.0f}});
        world.BuildBroadphase();
        owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
        player.SetPhysicsWorld(&world);
        player.SetDebugDrawEnabled(false);
        player.OnStart();

        for (int i = 0; i < 30 && !player.IsGrounded(); ++i)
            player.OnUpdate();
        return player;
    }
} // namespace

class PlayerComponentTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

TEST_F(PlayerComponentTest, DesiredSpeedScaleClampsToUnitRange)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 2.0f);

    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 1.0f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().x, 1.0f);
    EXPECT_FLOAT_EQ(player.DesiredDirection().z, 0.0f);
}

TEST_F(PlayerComponentTest, ClimbMoveClampsToSignedUnitRange)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.SetClimbMove(5.0f, -5.0f);

    EXPECT_FLOAT_EQ(player.ClimbRight(), 1.0f);
    EXPECT_FLOAT_EQ(player.ClimbForward(), -1.0f);
}

TEST_F(PlayerComponentTest, MaxSpeedRoundsNegativeAndDropsNonFinite)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.SetMaxSpeed(-3.0f);
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 0.0f);

    player.SetMaxSpeed(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 0.0f);

    player.SetMaxSpeed(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 0.0f);
}

// 調整値の読みは同居の組が正。ここが切れると Inspector で触っても手触りが変わらない
TEST_F(PlayerComponentTest, ReadsTuningFromSiblingStatsManager)
{
    GameObject obj;
    auto& stats = *obj.AddComponent<PlayerStatsManagerComponent>();
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.OnStart();
    EXPECT_FLOAT_EQ(player.CoyoteTime(), 0.025f);

    stats.SetCoyoteTime(0.2f);
    EXPECT_FLOAT_EQ(player.CoyoteTime(), 0.2f);
}

// 組を積まない検証台でも既定の組で動く。既定値は組の既定と同じなので手触りは変わらない
TEST_F(PlayerComponentTest, FallsBackToDefaultTuningWithoutStatsManager)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.OnStart();

    EXPECT_FLOAT_EQ(player.CoyoteTime(), 0.025f);
    EXPECT_FLOAT_EQ(player.Stats().walkSpeed, 4.0f);
}

TEST_F(PlayerComponentTest, ResetStateClearsMotion)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetVelocity(Vector3{3.0f, 9.0f, -2.0f});
    player.SetGrounded(true);
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);

    player.ResetState();

    EXPECT_FLOAT_EQ(player.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player.Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(player.Velocity().z, 0.0f);
    EXPECT_EQ(player.JumpsRemaining(), 1);
    EXPECT_FALSE(player.IsGrounded());
    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 0.0f);
}

TEST_F(PlayerComponentTest, GravityPullsHarderWhileFalling)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetVelocity(Vector3{0.0f, -5.0f, 0.0f});

    player.Gravity(k_FixedDt);

    EXPECT_NEAR(player.VerticalVelocity(), -5.0f + -35.0f * k_FixedDt, 1e-5f);
}

TEST_F(PlayerComponentTest, GravityIsHalvedNearTheApex)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetVelocity(Vector3{0.0f, 0.5f, 0.0f});

    player.Gravity(k_FixedDt);

    EXPECT_NEAR(player.VerticalVelocity(), 0.5f + -25.0f * 0.5f * k_FixedDt, 1e-5f);
}

TEST_F(PlayerComponentTest, GroundedJumpSpendsTheJump)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetGrounded(true);
    player.SetJumpPressed();

    player.Jump(k_FixedDt);

    EXPECT_FLOAT_EQ(player.VerticalVelocity(), 12.0f);
    EXPECT_EQ(player.JumpsRemaining(), 0);
}

TEST_F(PlayerComponentTest, JumpIsLostAfterTheCoyoteWindow)
{
    GameObject insideWindow;
    auto& early = *insideWindow.AddComponent<PlayerComponent>();
    early.SetGrounded(true);
    early.SyncGroundState();
    early.SetGrounded(false);
    early.TickTimers(k_FixedDt);
    early.SetJumpPressed();
    early.Jump(k_FixedDt);

    EXPECT_FLOAT_EQ(early.VerticalVelocity(), 12.0f);

    GameObject outsideWindow;
    auto& late = *outsideWindow.AddComponent<PlayerComponent>();
    late.SetGrounded(true);
    late.SyncGroundState();
    late.SetGrounded(false);
    late.TickTimers(k_FixedDt);
    late.TickTimers(k_FixedDt);
    late.SetJumpPressed();
    late.Jump(k_FixedDt);

    EXPECT_FLOAT_EQ(late.VerticalVelocity(), 0.0f);
    EXPECT_EQ(late.JumpsRemaining(), 1);
}

TEST_F(PlayerComponentTest, ReleasingTheButtonCutsTheRise)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetJumpHeld(true);
    player.OnUpdate();

    player.SetJumpHeld(false);
    player.SetVelocity(Vector3{0.0f, 10.0f, 0.0f});
    player.CutJumpRelease();

    EXPECT_FLOAT_EQ(player.VerticalVelocity(), 6.0f);
}

TEST_F(PlayerComponentTest, InputInsideTheDeadzoneAimsAtZeroSpeed)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.2f);

    player.AccelerateToInputDirection(k_FixedDt);

    EXPECT_FLOAT_EQ(player.Velocity().x, 0.0f);
}

TEST_F(PlayerComponentTest, HalfScaleSplitsWalkSpeedFromMaxSpeed)
{
    const float lag = 1.0f - std::exp(-k_FixedDt / 0.1f);

    GameObject walkObj;
    auto& walker = *walkObj.AddComponent<PlayerComponent>();
    walker.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.4f);
    walker.AccelerateToInputDirection(k_FixedDt);

    EXPECT_NEAR(walker.Velocity().x, 4.0f * lag, 1e-5f);

    GameObject runObj;
    auto& runner = *runObj.AddComponent<PlayerComponent>();
    runner.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.8f);
    runner.AccelerateToInputDirection(k_FixedDt);

    EXPECT_NEAR(runner.Velocity().x, 8.0f * 0.8f * lag, 1e-5f);
}

TEST_F(PlayerComponentTest, TapSlamFiresOnTheStepAfterTheRequest)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);
    ASSERT_TRUE(player.IsGrounded());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(0.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.0f);
    EXPECT_GT(player.Velocity().y, 0.0f);
    EXPECT_GT(player.Velocity().x, 5.0f);
    EXPECT_LT(player.Velocity().x, 15.0f);
}

TEST_F(PlayerComponentTest, ChargedSlamFiresWithTheRushSpeed)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 1.0f);
    EXPECT_GT(player.Velocity().x, 15.0f);
}

// 空中の押しを捨てると連打で出ない歩ができる。接地は求めない
TEST_F(PlayerComponentTest, SlamFiresInAir)
{
    GameObject obj;
    obj.AddComponent<NamedStateManager>();
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetDebugDrawEnabled(false);
    player.OnStart();
    ASSERT_FALSE(player.IsGrounded());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
}

// 出せない歩の押しをその場で捨てると連打が取りこぼされる。先行入力時間ぶん覚える
TEST_F(PlayerComponentTest, BufferedRequestSurvivesInsideTheWindow)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    player.RequestBodySlam(1.0f);
    for (int i = 0; i < 5; ++i)
        player.OnUpdate();
    ASSERT_FALSE(player.IsBodySlamming());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
}

// 覚え続けると忘れた頃に勝手に出る。先行入力時間で失効させる
TEST_F(PlayerComponentTest, BufferedRequestExpiresAfterTheBufferTime)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    player.RequestBodySlam(1.0f);
    for (int i = 0; i < 20; ++i)
        player.OnUpdate();
    ASSERT_FALSE(player.IsBodySlamming());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_FALSE(player.IsBodySlamming());
}

// 反発後の残り速度が向きに勝つと狙いと食い違う方へ飛ぶ。速度よりカメラの前が先
TEST_F(PlayerComponentTest, AimsAtTheCameraForwardWithoutInput)
{
    NS::Object::Scene scene;
    GameObject* obj = scene.SpawnTransient<GameObject>();
    ASSERT_NE(obj, nullptr);
    ASSERT_NE(scene.CameraBrain(), nullptr);
    ASSERT_NEAR(scene.CameraBrain()->ForwardHorizontal().z, 1.0f, 1.0e-4f);

    obj->AddComponent<NamedStateManager>();
    auto& player = *obj->AddComponent<PlayerComponent>();
    player.SetDebugDrawEnabled(false);
    player.OnStart();

    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_GT(player.Velocity().z, 15.0f);
    EXPECT_NEAR(player.Velocity().x, 0.0f, 1.0e-4f);
}

// カメラの居ない検証台でも突進が出せるよう、速度を最後の受けに残す
TEST_F(PlayerComponentTest, FallsBackToTheVelocityWithoutInputOrCamera)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    player.SetVelocity(Vector3{5.0f, 0.0f, 0.0f});
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamEntrySpeed(), 5.0f);
    EXPECT_GT(player.Velocity().x, 15.0f);
    EXPECT_NEAR(player.Velocity().z, 0.0f, 1.0e-4f);
}

// 長さ 0 のまま正規化すると 0 除算になる。向きが 1 つも決まらない歩は出さない
TEST_F(PlayerComponentTest, DoesNotFireWithoutAnyDirection)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_FALSE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamProgress01(), 0.0f);
}

// NaN は 0..1 への丸めを素通りして溜め量に残る
TEST_F(PlayerComponentTest, NonFiniteChargeIsTreatedAsTap)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(std::numeric_limits<float>::quiet_NaN());
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.0f);
    EXPECT_LT(player.Velocity().x, 15.0f);
}

// 動詞へ割った 1 歩が現行と 1 ビットも違わないことを見張る。値だけの検証は呼ぶ順序の入れ替えを拾えない
TEST_F(PlayerComponentTest, MatchesLegacyMovementStepForStep)
{
    NS::Physics::PhysicsWorld world;
    world.AddAABB(AABB{Vector3{0.0f, -1.0f, 0.0f}, Vector3{50.0f, 1.0f, 50.0f}});
    world.BuildBroadphase();

    GameObject legacyObj;
    auto& legacy = *legacyObj.AddComponent<NS::Object::CharacterMovementComponent>();
    legacy.SetPhysicsWorld(&world);
    legacy.SetDebugDrawEnabled(false);
    legacyObj.Root().SetPosition(Vector3{0.0f, 2.0f, 0.0f});

    GameObject freshObj;
    auto& fresh = *freshObj.AddComponent<PlayerComponent>();
    fresh.SetPhysicsWorld(&world);
    fresh.SetDebugDrawEnabled(false);
    freshObj.Root().SetPosition(Vector3{0.0f, 2.0f, 0.0f});

    for (int step = 0; step < 240; ++step)
    {
        const bool held = step >= 120 && step < 130;
        legacy.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.8f);
        fresh.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.8f);
        legacy.SetJumpHeld(held);
        fresh.SetJumpHeld(held);
        if (step == 120)
        {
            legacy.SetJumpPressed();
            fresh.SetJumpPressed();
        }

        legacy.OnUpdate();
        fresh.OnUpdate();

        const Vector3 legacyPos = legacyObj.Root().Position();
        const Vector3 freshPos = freshObj.Root().Position();
        ASSERT_EQ(freshPos.x, legacyPos.x) << step;
        ASSERT_EQ(freshPos.y, legacyPos.y) << step;
        ASSERT_EQ(freshPos.z, legacyPos.z) << step;
        ASSERT_EQ(fresh.Velocity().x, legacy.Velocity().x) << step;
        ASSERT_EQ(fresh.Velocity().y, legacy.Velocity().y) << step;
        ASSERT_EQ(fresh.Velocity().z, legacy.Velocity().z) << step;
        ASSERT_EQ(fresh.IsGrounded(), legacy.IsGrounded()) << step;
    }
}
