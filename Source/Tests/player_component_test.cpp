#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Game/Player/States/BodySlamPlayerState.h>
#include <Game/Player/States/BrakePlayerState.h>
#include <Game/Player/States/FallPlayerState.h>
#include <Game/Player/States/IdlePlayerState.h>
#include <Game/Player/States/LedgeClimbingPlayerState.h>
#include <Game/Player/States/LedgeHangingPlayerState.h>
#include <Game/Player/States/WalkPlayerState.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CameraBrainComponent.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsScene.h>

#include "entity_test_stage.h"
#include "jolt_test_scene.h"
#include "tuning_field_access.h"
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Player::BodySlamPlayerState;
    using NS::Game::Player::BrakePlayerState;
    using NS::Game::Player::FallPlayerState;
    using NS::Game::Player::IdlePlayerState;
    using NS::Game::Player::LedgeClimbingPlayerState;
    using NS::Game::Player::LedgeHangingPlayerState;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Game::Player::WalkPlayerState;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    // 調整値の欄名。そのままシーン JSON の鍵になるので、半角空白 1 つのずれでも値が読めなくなる
    const std::vector<std::string> k_TuningFieldNames = {"ジャンプ初速",
                                                         "上昇重力",
                                                         "下降重力",
                                                         "頂点滞空 Vy",
                                                         "頂点滞空倍率",
                                                         "ジャンプ離し倍率",
                                                         "コヨーテ時間",
                                                         "先行入力時間",
                                                         "歩き速度",
                                                         "走行速度",
                                                         "加速度",
                                                         "空中の加速度",
                                                         "曲がる時の抵抗",
                                                         "手を放した時の減速度",
                                                         "ブレーキの減速度",
                                                         "ブレーキのしきい値",
                                                         "スティック遊び",
                                                         "登れる段の高さ",
                                                         "掴める縁の下向き距離",
                                                         "縁へ手を伸ばす距離",
                                                         "よじ登りの所要時間",
                                                         "縁の横移動速度",
                                                         "振り向きの速さ",
                                                         "突進速度",
                                                         "突進距離",
                                                         "タップ初速",
                                                         "タップの上向き初速",
                                                         "タップ距離",
                                                         "狙いの巻き戻し秒",
                                                         "狙いの巻き戻しが消える秒"};

    using NsTest::ReadTuningField;
    using NsTest::WriteTuningField;

    //! 中心 (cx,cy,cz) に置いた 1m 立方の固形 block
    AABB MakeBlock(float cx, float cy, float cz)
    {
        return AABB{Vector3{cx, cy, cz}, Vector3{0.5f, 0.5f, 0.5f}};
    }

    //! 自機 2 部品を積んで OnStart まで通す。状態機械が欠けると遷移が 1 つも起きない
    //! @details 積む順は Player のコンストラクタと同じ
    PlayerComponent& MakePlayer(GameObject& owner)
    {
        auto& manager = *owner.AddComponent<PlayerStateManagerComponent>();
        auto& player = *owner.AddComponent<PlayerComponent>();

        player.OnStart();
        manager.OnStart();
        return player;
    }

    //! 床を敷かない検証台。1 フレーム目から下降するので掴みの条件が立つ。block は呼び出し側が積む
    PlayerComponent& MakeLedgeReady(GameObject& owner)
    {
        auto& player = MakePlayer(owner);
        // 立ちは縁掴みを持たず、立ちから始めると最初のフレームに掴まない
        // 状態機械は最初のフレームまで組まれないので、先に組んでから落下へ移す
        auto& manager = *owner.FindComponent<PlayerStateManagerComponent>();
        manager.EnsureBuilt(player);
        manager.Change<FallPlayerState>();
        return player;
    }

    [[nodiscard]] std::string_view CurrentStateName(GameObject& owner)
    {
        return owner.FindComponent<PlayerStateManagerComponent>()->CurrentName();
    }

    //! 床 1 枚を敷いて接地させた自機を返す。壁は呼び出し側が先に足す
    PlayerComponent& MakeSlamReady(GameObject& owner, NS::Physics::PhysicsScene& physics)
    {
        auto& player = MakePlayer(owner);

        NsTest::AddBox(physics, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{64.0f, 0.5f, 64.0f}});
        physics.OptimizeBroadPhase();
        owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});

        for (int i = 0; i < 30 && !player.IsGrounded(); ++i)
        {
            player.OnUpdate();
        }
        // 接地は動いた後に決まる。落下から立ちへ移るのは着地の次のフレーム
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

TEST_F(PlayerComponentTest, MaxSpeedRoundsNegativeAndKeepsTheValueOnNonFinite)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.SetMaxSpeed(20.0f);
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 20.0f);

    player.SetMaxSpeed(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 20.0f);

    player.SetMaxSpeed(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 20.0f);

    player.SetMaxSpeed(-3.0f);
    EXPECT_FLOAT_EQ(player.MaxSpeed(), 0.0f);
}

// 当たりの形は同居する CapsuleColliderComponent が正。移動側はコピーを持たず直接読む
TEST_F(PlayerComponentTest, AdoptsSiblingCapsuleColliderSize)
{
    GameObject obj;
    obj.AddComponent<NS::Object::CapsuleColliderComponent>(0.7f, 0.9f);
    auto& player = MakePlayer(obj);

    player.OnUpdate();

    EXPECT_FLOAT_EQ(player.CapsuleRadius(), 0.7f);
    EXPECT_FLOAT_EQ(player.CapsuleHalfHeight(), 0.9f);
}

TEST_F(PlayerComponentTest, CapsuleDefaultsWithoutCollider)
{
    PlayerComponent player;

    EXPECT_FLOAT_EQ(player.CapsuleRadius(), 0.4f);
    EXPECT_FLOAT_EQ(player.CapsuleHalfHeight(), 0.5f);
}

TEST_F(PlayerComponentTest, OnUpdateNoOpWhenInactive)
{
    GameObject obj;
    auto& player = MakePlayer(obj);
    player.SetActive(false);

    player.SetJumpPressed();
    player.OnUpdate();

    EXPECT_FLOAT_EQ(player.VerticalVelocity(), 0.0f);
}

// 1 フレームを動かすのは HandleMovement。状態の側で Move を呼ぶ形だと、足した状態で呼び忘れてもビルドが通る
TEST_F(PlayerComponentTest, StatesDecideTheVelocityAndTheFrameMoves)
{
    GameObject obj;
    auto& player = MakePlayer(obj);
    obj.FindComponent<PlayerStateManagerComponent>()->EnsureBuilt(player);

    player.SetVelocity(Vector3{6.0f, 0.0f, 0.0f});
    const float startX = obj.Root().Position().x;

    IdlePlayerState idle;
    idle.OnStep(player, k_FixedDt);

    EXPECT_FLOAT_EQ(obj.Root().Position().x, startX);

    player.OnUpdate();

    EXPECT_GT(obj.Root().Position().x, startX);
}

// 調整値は自分の欄。ここが切れると Inspector で触っても手触りが変わらない
TEST_F(PlayerComponentTest, ReadsTuningFromItsOwnFields)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    player.OnStart();
    EXPECT_FLOAT_EQ(ReadTuningField(player, "コヨーテ時間"), 0.025f);
    EXPECT_FLOAT_EQ(ReadTuningField(player, "歩き速度"), 4.0f);

    WriteTuningField(player, "コヨーテ時間", 0.2f);
    EXPECT_FLOAT_EQ(ReadTuningField(player, "コヨーテ時間"), 0.2f);
}

TEST_F(PlayerComponentTest, ReflectsEveryTuningFieldName)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    const NS::Object::ReflectionInfo* info = player.GetReflection();
    ASSERT_NE(info, nullptr);

    for (const std::string& name : k_TuningFieldNames)
        EXPECT_NE(NS::Object::FindField(info, name.c_str()), nullptr)
            << name << " の欄が無い。シーン JSON のこの値は警告だけ残して捨てられ、調整値が既定へ化ける";
}

TEST_F(PlayerComponentTest, ReadsTheJumpImpulseThroughReflection)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    EXPECT_FLOAT_EQ(ReadTuningField(player, "ジャンプ初速"), 12.0f);
}

TEST_F(PlayerComponentTest, TuningWriteThroughReflectionReachesMembers)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    WriteTuningField(player, "突進距離", 7.5f);
    WriteTuningField(player, "先行入力時間", 0.4f);

    EXPECT_FLOAT_EQ(ReadTuningField(player, "突進距離"), 7.5f);
    EXPECT_FLOAT_EQ(ReadTuningField(player, "先行入力時間"), 0.4f);
}

// 加速の上限は走行速度の欄と CollisionInputComponent が動かす。この自機に CollisionInputComponent は居ない
TEST_F(PlayerComponentTest, WritingRunSpeedRaisesTheAccelerationCap)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    WriteTuningField(player, "走行速度", 20.0f);

    EXPECT_FLOAT_EQ(player.MaxSpeed(), 20.0f);
}

TEST_F(PlayerComponentTest, TuningKeepsItsValueOnNonFiniteWrite)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();

    const float k_Rejected[] = {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()};

    for (const std::string& name : k_TuningFieldNames)
    {
        const float original = ReadTuningField(player, name.c_str());
        ASSERT_TRUE(std::isfinite(original)) << name;

        for (float rejected : k_Rejected)
        {
            WriteTuningField(player, name.c_str(), rejected);
            EXPECT_FLOAT_EQ(ReadTuningField(player, name.c_str()), original) << name;
        }
    }
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

// 着地するまで 2 回目は出ない。空中で押し続けると無限に登れる
TEST_F(PlayerComponentTest, SecondJumpDoesNotFireWithoutLanding)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    ASSERT_TRUE(player.IsGrounded());

    player.SetJumpPressed();
    player.OnUpdate();
    ASSERT_EQ(player.JumpsRemaining(), 0);

    player.SetJumpPressed();
    player.OnUpdate();

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
    GameObject walkObj;
    auto& walker = *walkObj.AddComponent<PlayerComponent>();
    walker.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.4f);
    for (int i = 0; i < 30; ++i)
    {
        walker.AccelerateToInputDirection(k_FixedDt);
    }

    EXPECT_FLOAT_EQ(walker.Velocity().x, 4.0f);

    GameObject runObj;
    auto& runner = *runObj.AddComponent<PlayerComponent>();
    runner.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.8f);
    for (int i = 0; i < 30; ++i)
    {
        runner.AccelerateToInputDirection(k_FixedDt);
    }

    EXPECT_FLOAT_EQ(runner.Velocity().x, 8.0f * 0.8f);
}

TEST_F(PlayerComponentTest, TapSlamFiresOnTheStepAfterTheRequest)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
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

TEST_F(PlayerComponentTest, SlamUsesTheAimMarkedAtPress)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.MarkBodySlamAim();
    player.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player.RequestBodySlam(0.0f);
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_GT(player.Velocity().x, 5.0f);
    EXPECT_NEAR(player.Velocity().z, 0.0f, 1e-3f);
}

TEST_F(PlayerComponentTest, StaleAimFallsBackToTheCurrentDirection)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.MarkBodySlamAim();
    for (int i = 0; i < 20; ++i)
        player.OnUpdate();

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player.RequestBodySlam(0.0f);
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_NEAR(player.Velocity().x, 0.0f, 1e-3f);
    EXPECT_GT(player.Velocity().z, 5.0f);
}

TEST_F(PlayerComponentTest, ChargedSlamFiresWithTheRushSpeed)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 1.0f);
    EXPECT_GT(player.Velocity().x, 15.0f);
}

// 空中の押しを捨てると連打で出ないフレームができる。接地は求めない
TEST_F(PlayerComponentTest, SlamFiresInAir)
{
    GameObject obj;
    auto& player = MakePlayer(obj);
    ASSERT_FALSE(player.IsGrounded());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
}

// 空中で押し続けると無限に出て 1 発の重みが消える。次は接地するまで出さない
TEST_F(PlayerComponentTest, SecondSlamDoesNotFireInAir)
{
    GameObject obj;
    auto& player = MakePlayer(obj);
    ASSERT_FALSE(player.IsGrounded());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    for (int i = 0; i < 120 && player.IsBodySlamming(); ++i)
        player.OnUpdate();
    ASSERT_FALSE(player.IsBodySlamming());
    ASSERT_FALSE(player.IsGrounded());

    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_FALSE(player.IsBodySlamming());
}

// 空中で使い切っても、足が地面に付けば次の 1 発が戻る
TEST_F(PlayerComponentTest, LandingRestoresTheSlam)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    ASSERT_TRUE(player.IsGrounded());

    player.SetJumpPressed();
    player.OnUpdate();
    ASSERT_FALSE(player.IsGrounded());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    for (int i = 0; i < 120 && player.IsBodySlamming(); ++i)
        player.OnUpdate();
    ASSERT_FALSE(player.IsBodySlamming());

    // 先行入力が残っていると着地のフレームで勝手に出て、接地で戻ったことの確認にならない
    for (int i = 0; i < 240 && !player.IsGrounded(); ++i)
        player.OnUpdate();
    ASSERT_TRUE(player.IsGrounded());

    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
}

// 地上の連打まで止めると走りの中で当て直せない。接地したままの突進は明けたフレームで続けて出せる
TEST_F(PlayerComponentTest, GroundedSlamsFireBackToBack)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    for (int i = 0; i < 120 && player.IsBodySlamming(); ++i)
        player.OnUpdate();
    ASSERT_FALSE(player.IsBodySlamming());
    ASSERT_TRUE(player.IsGrounded());

    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
}

// 進みを突進の速さから積むと、壁に押し付けたフレームも進んだ扱いになり、距離を走り切るまで突進が終わらない
TEST_F(PlayerComponentTest, SlamAgainstAWallEndsWithoutRunningTheFullDistance)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    // 壁の面は x=0.45。半径 0.4 の自機との隙間は 0.05
    NsTest::AddBox(physics, AABB{Vector3{0.95f, 1.0f, 0.0f}, Vector3{0.5f, 1.0f, 4.0f}});
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    int steps = 0;
    for (; steps < 60 && player.IsBodySlamming(); ++steps)
        player.OnUpdate();

    // 走り切ると 30 フレーム (距離 10 / 速さ 20)。10 未満なら進めない状態が続いて打ち切れている
    EXPECT_FALSE(player.IsBodySlamming());
    EXPECT_LT(steps, 10);
}

// 出せないフレームの押しをその場で捨てると連打が取りこぼされる。先行入力時間ぶん覚える
// Scene の中ではカメラの前が向きになるので、向きの無いフレームは Scene の外でしか作れない
TEST_F(PlayerComponentTest, BufferedRequestSurvivesInsideTheWindow)
{
    GameObject obj;
    auto& player = MakePlayer(obj);

    player.RequestBodySlam(1.0f);
    for (int i = 0; i < 5; ++i)
        player.OnUpdate();
    ASSERT_FALSE(player.IsBodySlamming());

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
}

// 覚え続けると忘れた頃に勝手に出る。先行入力時間で失効させる
// Scene の中ではカメラの前が向きになるので、向きの無いフレームは Scene の外でしか作れない
TEST_F(PlayerComponentTest, BufferedRequestExpiresAfterTheBufferTime)
{
    GameObject obj;
    auto& player = MakePlayer(obj);

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

    auto& player = MakePlayer(*obj);

    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_GT(player.Velocity().z, 15.0f);
    EXPECT_NEAR(player.Velocity().x, 0.0f, 1.0e-4f);
}

// カメラの居ない検証台でも突進が出せるよう、速度をフォールバックに残す
TEST_F(PlayerComponentTest, FallsBackToTheVelocityWithoutInputOrCamera)
{
    GameObject obj;
    auto& player = MakePlayer(obj);

    player.SetVelocity(Vector3{5.0f, 0.0f, 0.0f});
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_GT(player.Velocity().x, 15.0f);
    EXPECT_NEAR(player.Velocity().z, 0.0f, 1.0e-4f);
}

// 長さ 0 のまま正規化すると 0 除算になる。向きが 1 つも決まらないフレームは出さない
// Scene の中ではカメラの前が向きになるので、向きの無いフレームは Scene の外でしか作れない
TEST_F(PlayerComponentTest, DoesNotFireWithoutAnyDirection)
{
    GameObject obj;
    auto& player = MakePlayer(obj);

    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_FALSE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamProgress01(), 0.0f);
}

// NaN は 0..1 への丸めを素通りして溜め量に残る
TEST_F(PlayerComponentTest, NonFiniteChargeIsTreatedAsTap)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(std::numeric_limits<float>::quiet_NaN());
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 0.0f);
    EXPECT_LT(player.Velocity().x, 15.0f);
}

// 突進中に曲がれると当てる間合いを詰める意味が消える
TEST_F(PlayerComponentTest, RushIgnoresDirectionInput)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int i = 0; i < 3; ++i)
        player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_GT(player.Velocity().x, 15.0f);
    EXPECT_NEAR(player.Velocity().z, 0.0f, 1.0e-4f);
}

TEST_F(PlayerComponentTest, RushEndsAfterTheRushDistance)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    const float startX = obj.Root().Position().x;

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    int steps = 0;
    while (player.IsBodySlamming() && steps < 120)
    {
        player.OnUpdate();
        ++steps;
    }

    EXPECT_LT(steps, 120);
    EXPECT_GT(obj.Root().Position().x - startX, 5.0f);
}

// 壁で止められると距離が減らず突進から出られなくなる。進めないフレームで打ち切る
TEST_F(PlayerComponentTest, RushEndsWhenTheWallStopsIt)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, AABB{Vector3{2.0f, 1.0f, 0.0f}, Vector3{0.5f, 2.0f, 8.0f}});
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    int steps = 0;
    while (player.IsBodySlamming() && steps < 120)
    {
        player.OnUpdate();
        ++steps;
    }

    EXPECT_LT(steps, 18);
    EXPECT_LT(obj.Root().Position().x, 2.0f);
}

// 短押しは隙の小さい移動技。突進より短い距離で終わる
TEST_F(PlayerComponentTest, TapHopEndsAfterTheShortDistance)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    const float startX = obj.Root().Position().x;

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(0.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    int steps = 0;
    while (player.IsBodySlamming() && steps < 120)
    {
        player.OnUpdate();
        ++steps;
    }

    EXPECT_LT(steps, 120);
    // 目標を越えたフレームで終わるので少し行き過ぎる。実移動で測る
    const float travelled = obj.Root().Position().x - startX;
    EXPECT_NEAR(travelled, ReadTuningField(player, "タップ距離"), 0.2f);
    EXPECT_LT(ReadTuningField(player, "タップ距離"), ReadTuningField(player, "突進距離"));
}

// 途中で着地すると残りを地面の上で滑り、走っていないのに動いて見える
TEST_F(PlayerComponentTest, TapSlamStaysAirborneUntilTheEndOfTheLunge)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    const Vector3 start = obj.Root().Position();

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(0.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    const float half = ReadTuningField(player, "タップ距離") * 0.5f;
    float peakY = start.y;
    bool groundedAtHalf = true;
    bool sawHalf = false;
    bool groundedAtEnd = false;
    int steps = 0;
    while (player.IsBodySlamming() && steps < 300)
    {
        player.OnUpdate();
        ++steps;
        const Vector3 position = obj.Root().Position();
        peakY = std::max(peakY, position.y);
        if (!sawHalf && position.x - start.x >= half)
        {
            sawHalf = true;
            groundedAtHalf = player.IsGrounded();
        }
        groundedAtEnd = player.IsGrounded();
    }
    ASSERT_LT(steps, 300);
    ASSERT_TRUE(sawHalf);

    // 半ばで足が着いていると残りを地面の上で滑る
    EXPECT_FALSE(groundedAtHalf);
    EXPECT_TRUE(groundedAtEnd);
    // ジャンプに見える高さまで上げると別の技になる
    EXPECT_LT(peakY - start.y, 0.7f);
}

TEST_F(PlayerComponentTest, ProgressRisesThenCancelResets)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    EXPECT_FLOAT_EQ(player.BodySlamProgress01(), 0.0f);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    float previous = player.BodySlamProgress01();
    for (int i = 0; i < 5; ++i)
    {
        player.OnUpdate();
        const float now = player.BodySlamProgress01();
        EXPECT_GT(now, previous);
        previous = now;
    }

    player.CancelBodySlam();
    EXPECT_FALSE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamProgress01(), 0.0f);
}

// 踏み込みは先行入力時間より長い。突進中に期限を数えると、明ける前に押しが消える
TEST_F(PlayerComponentTest, BufferedRequestSurvivesALongerRush)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(0.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    // 先行入力時間より長く突進させてから押す
    const int stepsPastBuffer = static_cast<int>(ReadTuningField(player, "先行入力時間") / k_FixedDt) + 2;
    for (int i = 0; i < stepsPastBuffer; ++i)
        player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    player.RequestBodySlam(1.0f);
    for (int i = 0; i < stepsPastBuffer; ++i)
        player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    for (int i = 0; i < 120 && player.BodySlamCharge01() < 1.0f; ++i)
        player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 1.0f);
}

// 突進中の押しをそのフレームで捨てると連打が取りこぼされる。突進明けのフレームで消費する
TEST_F(PlayerComponentTest, BufferedRequestFiresWhenTheRushEnds)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(0.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());
    ASSERT_FLOAT_EQ(player.BodySlamCharge01(), 0.0f);

    for (int i = 0; i < 8; ++i)
        player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());
    player.RequestBodySlam(1.0f);

    for (int i = 0; i < 120 && player.BodySlamCharge01() < 1.0f; ++i)
        player.OnUpdate();

    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 1.0f);
}

// 1 フレームが状態機械を通っているかを状態名で見る。値だけでは 1 本道のままでも同じ結果になる
TEST_F(PlayerComponentTest, StaysIdleWhileGroundedWithoutInput)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    ASSERT_TRUE(player.IsGrounded());

    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);
}

TEST_F(PlayerComponentTest, MovesToWalkWhileTheRunInputIsHeld)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    ASSERT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), WalkPlayerState::k_Name);
}

// 走りの 8 m/s から手を放すと、減速度 40 で 12 フレーム目にちょうど止まって立ちへ移る
TEST_F(PlayerComponentTest, ReleasingTheStickStopsAtExactlyZero)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), WalkPlayerState::k_Name);

    player.SetVelocity(Vector3{8.0f, 0.0f, 0.0f});
    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    for (int i = 0; i < 11; ++i)
    {
        player.OnUpdate();
    }
    ASSERT_EQ(CurrentStateName(obj), WalkPlayerState::k_Name);

    player.OnUpdate();

    EXPECT_EQ(player.LateralVelocity().x, 0.0f);
    EXPECT_EQ(player.LateralVelocity().z, 0.0f);
    EXPECT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);
}

// 空中は減速しない。手を放しても弾かれた勢いが残る
TEST_F(PlayerComponentTest, FallKeepsHorizontalSpeedWithoutInput)
{
    GameObject obj;
    auto& player = MakePlayer(obj);
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);

    player.SetVelocity(Vector3{5.0f, 0.0f, 0.0f});
    player.OnUpdate();

    EXPECT_FLOAT_EQ(player.Velocity().x, 5.0f);
}

// 最高速以上で進んでいる向きへ倒しても加速は足さず、最高速を超えた速さは削られない
TEST_F(PlayerComponentTest, HoldingTheStickKeepsSpeedAboveTheTop)
{
    GameObject obj;
    auto& player = MakePlayer(obj);
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);

    player.SetVelocity(Vector3{20.0f, 0.0f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_FLOAT_EQ(player.Velocity().x, 20.0f);
}

// 走っている向きと逆へ倒すとブレーキを挟み、止まってから立ちへ移る
TEST_F(PlayerComponentTest, ReverseInputBrakesToAStop)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    player.SetVelocity(Vector3{8.0f, 0.0f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), WalkPlayerState::k_Name);

    player.SetDesiredMove(Vector3{-1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), BrakePlayerState::k_Name);

    for (int i = 0; i < 30 && CurrentStateName(obj) == BrakePlayerState::k_Name; ++i)
    {
        player.OnUpdate();
    }

    EXPECT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);
    EXPECT_EQ(player.LateralVelocity().x, 0.0f);
}

// 突進の終わりに走行の最高速で切る。切らないと、倒している間は突進の速さのまま走り続ける
TEST_F(PlayerComponentTest, SlamThatHitsNothingEndsAtTheTopSpeed)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    for (int i = 0; i < 120 && player.IsBodySlamming(); ++i)
    {
        player.OnUpdate();
    }

    ASSERT_FALSE(player.IsBodySlamming());
    const Vector3 v = player.Velocity();
    EXPECT_LE(std::sqrt(v.x * v.x + v.z * v.z), player.RunSpeed() + 1e-4f);
}

TEST_F(PlayerComponentTest, MovesToFallWithoutGround)
{
    GameObject obj;
    auto& player = MakePlayer(obj);

    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);
}

// 押したフレームに移らないと突進の初速がそのフレームに乗らない
TEST_F(PlayerComponentTest, MovesToBodySlamOnTheStepOfTheRequest)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), BodySlamPlayerState::k_Name);
}

TEST_F(PlayerComponentTest, ResetStateReturnsToTheFirstState)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), WalkPlayerState::k_Name);

    player.ResetState();

    EXPECT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);
}

// 立ちは地上の状態なので縁掴みを持たない。空中に出たフレームは落下へ移すだけで、掴むのは次のフレーム
TEST_F(PlayerComponentTest, IdleLeavesTheLedgeGrabToFall)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakePlayer(obj);
    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetVelocity(Vector3{1.0f, 0.0f, 0.0f});

    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);

    player.OnUpdate();
    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

TEST_F(PlayerComponentTest, GrabsLedgeWhenDescendingIntoEdge)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    // 掴んだ後も 1 フレーム動かす。壁からはキャラクタの余白 2 cm ぶん離れて止まる
    EXPECT_NEAR(obj.Root().Position().x, -0.9f, 0.05f);
    EXPECT_NEAR(obj.Root().Position().y, 0.0f, 1e-3f);
    EXPECT_FLOAT_EQ(player.Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player.Velocity().y, 0.0f);
}

TEST_F(PlayerComponentTest, DoesNotGrabWhileGrounded)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.SetGrounded(true);

    EXPECT_FALSE(player.LedgeGrab());
}

TEST_F(PlayerComponentTest, DoesNotGrabWhileAscending)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.SetVelocity(Vector3{0.0f, 6.0f, 0.0f});
    player.OnUpdate();

    ASSERT_GT(player.Velocity().y, 0.0f);
    EXPECT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

TEST_F(PlayerComponentTest, DoesNotGrabWithoutHorizontalMovement)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    player.OnUpdate();

    EXPECT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

TEST_F(PlayerComponentTest, GrabsWithoutInputWhileMovingIntoTheLedge)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetVelocity(Vector3{4.0f, 0.0f, 0.0f});
    player.OnUpdate();
    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

// 手の高さの帯を外れた縁は掴まない。手が block 上端より 2m 上にある位置から前へ押しても素通りする
TEST_F(PlayerComponentTest, DoesNotGrabOutsideTheHandBand)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 2.0f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

// 帯の上は今フレーム動いた距離まで。手が届いていない縁へは体を引き上げない
TEST_F(PlayerComponentTest, DoesNotGrabALedgeAboveTheHand)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, -0.3f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

// 登り先が別の block で塞がれた縁は掴まない。オーバーハングの下でぶら下がったまま出られなくなる
TEST_F(PlayerComponentTest, DoesNotGrabWhenTheClimbTargetIsBlocked)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    NsTest::AddBox(physics, MakeBlock(0.0f, 1.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    EXPECT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

TEST_F(PlayerComponentTest, HangHoldsTheLedgeHeightWithoutGravity)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    const Vector3 hangPos = obj.Root().Position();

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    for (int i = 0; i < 10; ++i)
        player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    EXPECT_FLOAT_EQ(obj.Root().Position().y, hangPos.y);
    EXPECT_FLOAT_EQ(player.Velocity().y, 0.0f);
}

// 前入力で待たずに登る。LedgeGrab は入力を見ないので、倒さずに寄ればぶら下がったまま止まれる
TEST_F(PlayerComponentTest, ForwardInputClimbsImmediately)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(0.0f, 1.0f);
    player.OnUpdate();
    EXPECT_EQ(CurrentStateName(obj), LedgeClimbingPlayerState::k_Name);

    for (int i = 0; i < 20; ++i)
    {
        player.OnUpdate();
    }

    EXPECT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);
    EXPECT_TRUE(player.IsGrounded());
    EXPECT_EQ(player.JumpsRemaining(), 1);
    EXPECT_GT(obj.Root().Position().y, 0.5f);
}

TEST_F(PlayerComponentTest, NonPositiveClimbDurationFinishesTheClimbAtOnce)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);
    WriteTuningField(player, "よじ登りの所要時間", -0.25f);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(0.0f, 1.0f);
    player.OnUpdate();
    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);
}

TEST_F(PlayerComponentTest, JumpFromTheLedgeGoesStraightUp)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    const float hangY = obj.Root().Position().y;

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetJumpPressed();
    player.OnUpdate();

    EXPECT_NE(CurrentStateName(obj), LedgeClimbingPlayerState::k_Name);
    EXPECT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    EXPECT_GT(player.Velocity().y, 0.0f);
    EXPECT_FALSE(player.IsGrounded());

    for (int i = 0; i < 5; ++i)
    {
        player.OnUpdate();
    }

    EXPECT_GT(obj.Root().Position().y, hangY);
    EXPECT_NEAR(obj.Root().Position().x, -0.9f, 0.05f);
}

TEST_F(PlayerComponentTest, StandsOnTheTopAfterClimbing)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    NsTest::AddBox(physics, MakeBlock(1.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(0.0f, 1.0f);
    for (int i = 0; i < 30 && CurrentStateName(obj) != IdlePlayerState::k_Name; ++i)
    {
        player.OnUpdate();
    }
    ASSERT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name);
    player.SetClimbMove(0.0f, 0.0f);

    const float restY = 0.5f + player.CapsuleHalfHeight() + player.CapsuleRadius();
    for (int i = 0; i < 30; ++i)
    {
        player.OnUpdate();
        EXPECT_TRUE(player.IsGrounded()) << "登り切ってから " << i << " フレーム目";
        EXPECT_EQ(CurrentStateName(obj), IdlePlayerState::k_Name) << "登り切ってから " << i << " フレーム目";
    }
    EXPECT_NEAR(obj.Root().Position().y, restY, 1e-3f);
}

TEST_F(PlayerComponentTest, ReleaseButtonDropsFromTheLedge)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetReleaseLedgePressed();
    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);
    // 掴んでいる間も動かす。壁からはキャラクタの余白 2 cm ぶん離れた所で手を放す
    EXPECT_NEAR(obj.Root().Position().x, -0.9f, 0.05f);
    EXPECT_FLOAT_EQ(player.Velocity().x, 0.0f);
    EXPECT_FALSE(player.IsGrounded());
}

// 手放しは専用ボタンだけ。後ろ入力で放すと、カメラ側の縁へ寄せた入力で手を放す
TEST_F(PlayerComponentTest, BackInputKeepsHangingOnTheLedge)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    for (int i = 0; i < 30; ++i)
    {
        player.SetClimbMove(0.0f, -1.0f);
        player.OnUpdate();
        ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name) << "後ろ入力 " << i << " フレーム目";
    }
}

TEST_F(PlayerComponentTest, GrabsTheLedgeWhenFallingPastItInOneFrame)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    const float blockTop = 0.5f;
    obj.Root().SetPosition(Vector3{-0.9f, blockTop + 0.3f - player.CapsuleHalfHeight(), 0.0f});
    player.SetVelocity(Vector3{0.0f, -40.0f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

TEST_F(PlayerComponentTest, DoesNotRegrabAfterReleasingWithoutInput)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetReleaseLedgePressed();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);

    for (int i = 0; i < 60; ++i)
    {
        player.OnUpdate();
        ASSERT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name) << "手放してから " << i << " フレーム目";
    }
}

// 手放した後に壁へ倒し直しても、落ちている間は同じ縁を掴み直さない。掴み直すと縁から離れられない
TEST_F(PlayerComponentTest, DoesNotRegrabWhileFallingPastTheLedge)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetReleaseLedgePressed();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int i = 0; i < 60; ++i)
    {
        player.OnUpdate();
        EXPECT_NE(CurrentStateName(obj), LedgeHangingPlayerState::k_Name) << "手放してから " << i << " フレーム目";
    }
}

// 0 以下はその場で向く。手放した次のフレームで壁を向き、同じ縁を掴む
TEST_F(PlayerComponentTest, ZeroTurnSpeedFacesTheMoveAtOnce)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);
    WriteTuningField(player, "振り向きの速さ", 0.0f);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetReleaseLedgePressed();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), FallPlayerState::k_Name);

    // 向きが決まるのは動かす所。壁を向いた向きで掴みを試すのはその次のフレーム
    player.OnUpdate();
    player.OnUpdate();
    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
}

TEST_F(PlayerComponentTest, ShimmyMovesAlongTheLedge)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 1.0f));
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, -1.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    const float zStart = obj.Root().Position().z;

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(1.0f, 0.0f);
    for (int i = 0; i < 20; ++i)
        player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    EXPECT_GT(std::abs(obj.Root().Position().z - zStart), 0.4f);
}

TEST_F(PlayerComponentTest, ShimmyFollowsTheNextLedgeHeight)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    NsTest::AddBox(physics, MakeBlock(0.0f, -0.2f, -1.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(1.0f, 0.0f);
    for (int i = 0; i < 30; ++i)
    {
        player.OnUpdate();
    }

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    EXPECT_LT(obj.Root().Position().z, -0.5f);
    EXPECT_NEAR(obj.Root().Position().y, 0.3f - player.CapsuleHalfHeight(), 1e-3f);
}

TEST_F(PlayerComponentTest, ShimmyStopsAtTheLedgeEnd)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(1.0f, 0.0f);
    for (int i = 0; i < 60; ++i)
        player.OnUpdate();

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    EXPECT_LE(std::abs(obj.Root().Position().z), 0.55f);
}

// パッドの遊びは Input.cpp の NormalizeStick で掛かる。掴まり中の横移動で重ねると、弱く倒した時だけ動かない
TEST_F(PlayerComponentTest, ShimmyMovesWithWeakInput)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 1.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    const float zStart = obj.Root().Position().z;

    // 0.3 はパッドの遊び 7849 / 32767 = 0.24 を越えた直後に届く値
    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(0.3f, 0.0f);
    for (int i = 0; i < 20; ++i)
    {
        player.OnUpdate();
    }

    EXPECT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    EXPECT_GT(std::abs(obj.Root().Position().z - zStart), 0.1f);
}

TEST_F(PlayerComponentTest, ShimmyStaysWithoutInput)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Physics::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 1.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(CurrentStateName(obj), LedgeHangingPlayerState::k_Name);
    const float zStart = obj.Root().Position().z;

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(0.0f, 0.0f);
    for (int i = 0; i < 20; ++i)
    {
        player.OnUpdate();
    }

    EXPECT_FLOAT_EQ(obj.Root().Position().z, zStart);
}
