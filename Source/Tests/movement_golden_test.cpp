#include "golden_trace.h"

#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>

#include "entity_test_stage.h"
#include "jolt_test_world.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Object::GameObject;
    using NS::Tests::CompareTraces;
    using NS::Tests::DescribeDiff;
    using NS::Tests::FoldTrace;
    using NS::Tests::LoadBaseline;
    using NS::Tests::MissingBaselineMessage;
    using NS::Tests::SaveBaseline;
    using NS::Tests::StepRecord;
    using NS::Tests::TraceDiff;
    using NS::Tests::TraceTolerance;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr TraceTolerance k_Exact{};

    float MaxHeight(const std::vector<StepRecord>& trajectory) noexcept
    {
        float peak = -1000.0f;
        for (const StepRecord& s : trajectory)
            peak = std::max(s.position.y, peak);
        return peak;
    }

    //! ジャンプ発動の検出。ジャンプ初速 12 の上向き速度は自由落下では絶対に出ない
    bool HasUpwardBurst(const std::vector<StepRecord>& trajectory) noexcept
    {
        for (const StepRecord& s : trajectory)
        {
            if (s.velocity.y > 5.0f)
                return true;
        }
        return false;
    }

    //! 自機 2 部品を載せて OnStart まで通す。積む順は Player のコンストラクタと同じ
    //! @details 開始位置は空中に取り、数ステップの自然落下で着地させる
    PlayerComponent& SetUpMovement(GameObject& owner, NS::Physics::PhysicsWorld& world, const Vector3& startPosition)
    {
        auto& manager = *owner.AddComponent<PlayerStateManagerComponent>();
        auto& movement = *owner.AddComponent<PlayerComponent>();

        owner.Root().SetPosition(startPosition);
        world.OptimizeBroadPhase();
        movement.OnStart();
        manager.OnStart();
        return movement;
    }

    StepRecord Record(GameObject& owner, const PlayerComponent& movement)
    {
        return StepRecord{owner.Root().Position(), movement.Velocity(), movement.IsGrounded()};
    }

    //! 平地を X+ へ全開で走り、90 ステップ目から入力を切って停止する
    //! 加速の立ち上がり・最大速度巡航・減速の 3 経路を通す
    std::vector<StepRecord> RunFlatWalk()
    {
        NsTest::EntityStage stage;
        GameObject& owner = stage.owner;
        NS::Physics::PhysicsWorld& world = stage.world;
        NsTest::AddBox(world, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{16.0f, 0.5f, 8.0f}});
        auto& movement = SetUpMovement(owner, world, Vector3{0.0f, 1.0f, 0.0f});

        std::vector<StepRecord> trajectory;
        for (int i = 0; i < 120; ++i)
        {
            if (i < 90)
                movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            else
                movement.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
            movement.OnUpdate();
            trajectory.push_back(Record(owner, movement));
        }
        return trajectory;
    }

    //! 走りながらジャンプ 1 回。20 ステップ保持してから離すことで
    //! 上昇の弱い重力・離し減速・頂点の重力緩和・落下の強い重力を全て通す
    //! 床は 3 秒間の全力走行で走り抜けない長さにする
    std::vector<StepRecord> RunSingleJump()
    {
        NsTest::EntityStage stage;
        GameObject& owner = stage.owner;
        NS::Physics::PhysicsWorld& world = stage.world;
        NsTest::AddBox(world, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{32.0f, 0.5f, 8.0f}});
        auto& movement = SetUpMovement(owner, world, Vector3{0.0f, 1.0f, 0.0f});

        std::vector<StepRecord> trajectory;
        for (int i = 0; i < 180; ++i)
        {
            movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            if (i == 30)
                movement.SetJumpPressed();
            movement.SetJumpHeld(i >= 30 && i < 50);
            movement.OnUpdate();
            trajectory.push_back(Record(owner, movement));
        }
        return trajectory;
    }

    //! 短い床を走り抜けて踏み外し、その次のステップでジャンプ入力する
    //! 入力の時点で空中 1 ステップぶんの時間が経過しており、コヨーテ時間の内側を踏む
    std::vector<StepRecord> RunCoyoteJump()
    {
        NsTest::EntityStage stage;
        GameObject& owner = stage.owner;
        NS::Physics::PhysicsWorld& world = stage.world;
        NsTest::AddBox(world, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{2.0f, 0.5f, 8.0f}});
        auto& movement = SetUpMovement(owner, world, Vector3{0.0f, 1.0f, 0.0f});

        std::vector<StepRecord> trajectory;
        bool prevGrounded = false;
        bool pressQueued = false;
        bool jumped = false;
        for (int i = 0; i < 150; ++i)
        {
            if (pressQueued && !jumped)
            {
                movement.SetJumpPressed();
                movement.SetJumpHeld(true);
                jumped = true;
            }
            movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            movement.OnUpdate();

            const bool nowGrounded = movement.IsGrounded();
            if (prevGrounded && !nowGrounded && !jumped)
                pressQueued = true;
            prevGrounded = nowGrounded;

            trajectory.push_back(Record(owner, movement));
        }
        return trajectory;
    }

    //! 高所から自由落下し、着地前の空中でジャンプ入力を出す
    //! 着地の瞬間に先行入力が消費されて即ジャンプする経路を固定する
    std::vector<StepRecord> RunJumpBuffer()
    {
        NsTest::EntityStage stage;
        GameObject& owner = stage.owner;
        NS::Physics::PhysicsWorld& world = stage.world;
        NsTest::AddBox(world, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{8.0f, 0.5f, 8.0f}});
        auto& movement = SetUpMovement(owner, world, Vector3{0.0f, 3.0f, 0.0f});

        std::vector<StepRecord> trajectory;
        bool pressed = false;
        for (int i = 0; i < 180; ++i)
        {
            const bool falling = !movement.IsGrounded() && movement.Velocity().y < 0.0f;
            if (!pressed && falling && owner.Root().Position().y < 1.2f)
            {
                movement.SetJumpPressed();
                movement.SetJumpHeld(true);
                pressed = true;
            }
            movement.OnUpdate();
            trajectory.push_back(Record(owner, movement));
        }
        return trajectory;
    }

    //! 壁へ向かって走り続け、衝突解決で壁面手前に止まる押し戻しを固定する
    std::vector<StepRecord> RunWallCollision()
    {
        NsTest::EntityStage stage;
        GameObject& owner = stage.owner;
        NS::Physics::PhysicsWorld& world = stage.world;
        NsTest::AddBox(world, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{8.0f, 0.5f, 8.0f}});
        NsTest::AddBox(world, AABB{Vector3{6.0f, 1.5f, 0.0f}, Vector3{0.5f, 2.0f, 8.0f}});
        auto& movement = SetUpMovement(owner, world, Vector3{0.0f, 1.0f, 0.0f});

        std::vector<StepRecord> trajectory;
        for (int i = 0; i < 120; ++i)
        {
            movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            movement.OnUpdate();
            trajectory.push_back(Record(owner, movement));
        }
        return trajectory;
    }

    //! 縁を掴む → シミー → よじ登る → 立つ を 1 続きで通す
    //! 床を敷かないので 1 歩目から下降し、掴みの条件が立つ
    //! 掴まりは値を見る検証しか持たず、呼ぶ順序の入れ替えは基準の軌跡でしか拾えない
    std::vector<StepRecord> RunLedgeClimb()
    {
        NsTest::EntityStage stage;
        GameObject& owner = stage.owner;
        NS::Physics::PhysicsWorld& world = stage.world;
        NsTest::AddBox(world, AABB{Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}});
        NsTest::AddBox(world, AABB{Vector3{0.0f, 0.0f, 1.0f}, Vector3{0.5f, 0.5f, 0.5f}});
        NsTest::AddBox(world, AABB{Vector3{0.0f, 0.0f, -1.0f}, Vector3{0.5f, 0.5f, 0.5f}});
        auto& movement = SetUpMovement(owner, world, Vector3{-0.9f, 0.0f, 0.0f});

        std::vector<StepRecord> trajectory;
        for (int i = 0; i < 150; ++i)
        {
            float speedScale = 0.0f;
            if (i == 0)
                speedScale = 1.0f;

            float climb = 0.0f;
            if (i >= 1)
                climb = 1.0f;

            movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, speedScale);
            movement.SetClimbMove(climb, climb);
            movement.OnUpdate();
            trajectory.push_back(Record(owner, movement));
        }
        return trajectory;
    }

    std::vector<StepRecord> RunSlopeAscent()
    {
        constexpr float k_Pi = 3.14159265358979323846f;
        constexpr float k_AngleDegrees = 30.0f;
        constexpr float k_Length = 24.0f;
        constexpr float k_HalfWidth = 4.0f;
        const float height = std::tan(k_AngleDegrees * k_Pi / 180.0f) * k_Length;
        const Vector3 lowLeft{-k_HalfWidth, 0.0f, -k_Length * 0.5f};
        const Vector3 lowRight{k_HalfWidth, 0.0f, -k_Length * 0.5f};
        const Vector3 highLeft{-k_HalfWidth, height, k_Length * 0.5f};
        const Vector3 highRight{k_HalfWidth, height, k_Length * 0.5f};

        NsTest::EntityStage stage;
        GameObject& owner = stage.owner;
        NS::Physics::PhysicsWorld& world = stage.world;
        const std::array<NS::Physics::Triangle, 2> slope{
            NS::Physics::Triangle{lowLeft, highRight, lowRight},
            NS::Physics::Triangle{lowLeft, highLeft, highRight},
        };
        world.AddMesh(slope, NS::Physics::ObjectLayers::Terrain);
        auto& movement = SetUpMovement(owner, world, Vector3{0.0f, 2.5f, -10.5f});

        std::vector<StepRecord> trajectory;
        for (int i = 0; i < 120; ++i)
        {
            movement.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
            movement.OnUpdate();
            trajectory.push_back(Record(owner, movement));
        }
        return trajectory;
    }
} // namespace

class MovementGolden : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

//! 基準の軌跡は差 0 で突き合わせる。同じビルドで 2 回走らせた結果がビット一致することが前提
TEST_F(MovementGolden, HashIsStableAcrossTwoRuns)
{
    EXPECT_EQ(FoldTrace(RunSingleJump()), FoldTrace(RunSingleJump()));
}

TEST_F(MovementGolden, FlatWalkMatchesGoldenTrace)
{
    const auto trajectory = RunFlatWalk();

    float maxVx = 0.0f;
    for (const StepRecord& s : trajectory)
        maxVx = std::max(s.velocity.x, maxVx);
    EXPECT_GT(maxVx, 6.0f) << "巡航速度が最大速度 8 に届いていない";
    EXPECT_LT(maxVx, 9.0f) << "最大速度 8 を大きく超えている";
    EXPECT_GT(trajectory.back().position.x, 8.0f) << "速度が出ているのに位置が前進していない";
    EXPECT_LT(trajectory.back().velocity.x, 0.5f) << "入力を切った後に停止していない";
    EXPECT_TRUE(trajectory.back().grounded);

    const auto baseline = LoadBaseline("movement_flat_walk");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("movement_flat_walk");
    const TraceDiff diff = CompareTraces(*baseline, trajectory, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trajectory);
}

TEST_F(MovementGolden, SingleJumpMatchesGoldenTrace)
{
    const auto trajectory = RunSingleJump();

    EXPECT_GT(MaxHeight(trajectory), 2.0f) << "ジャンプ頂点が低すぎる";
    EXPECT_LT(MaxHeight(trajectory), 6.0f) << "ジャンプ頂点が高すぎる";
    EXPECT_TRUE(trajectory.back().grounded) << "着地して終わっていない";

    const auto baseline = LoadBaseline("movement_single_jump");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("movement_single_jump");
    const TraceDiff diff = CompareTraces(*baseline, trajectory, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trajectory);
}

TEST_F(MovementGolden, CoyoteJumpMatchesGoldenTrace)
{
    const auto trajectory = RunCoyoteJump();

    EXPECT_TRUE(HasUpwardBurst(trajectory)) << "踏み外し後の猶予ジャンプが発動していない";

    const auto baseline = LoadBaseline("movement_coyote_jump");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("movement_coyote_jump");
    const TraceDiff diff = CompareTraces(*baseline, trajectory, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trajectory);
}

TEST_F(MovementGolden, JumpBufferMatchesGoldenTrace)
{
    const auto trajectory = RunJumpBuffer();

    EXPECT_TRUE(HasUpwardBurst(trajectory)) << "着地時に先行入力ジャンプが発動していない";

    const auto baseline = LoadBaseline("movement_jump_buffer");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("movement_jump_buffer");
    const TraceDiff diff = CompareTraces(*baseline, trajectory, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trajectory);
}

TEST_F(MovementGolden, WallCollisionMatchesGoldenTrace)
{
    const auto trajectory = RunWallCollision();

    EXPECT_LT(trajectory.back().position.x, 5.5f) << "壁にめり込んでいる";
    EXPECT_GT(trajectory.back().position.x, 4.0f) << "壁のはるか手前で止まっている";
    EXPECT_LT(trajectory.back().velocity.x, 0.5f) << "壁に当たり続けているのに速度が残っている";

    const auto baseline = LoadBaseline("movement_wall_collision");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("movement_wall_collision");
    const TraceDiff diff = CompareTraces(*baseline, trajectory, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trajectory);
}

TEST_F(MovementGolden, LedgeClimbMatchesGoldenTrace)
{
    const auto trajectory = RunLedgeClimb();

    EXPECT_TRUE(trajectory.back().grounded) << "よじ登り切って立っていない";
    EXPECT_GT(trajectory.back().position.y, 0.5f) << "上面へ上がっていない";

    const auto baseline = LoadBaseline("movement_ledge_climb");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("movement_ledge_climb");
    const TraceDiff diff = CompareTraces(*baseline, trajectory, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trajectory);
}

TEST_F(MovementGolden, SlopeAscentMatchesGoldenTrace)
{
    const auto trajectory = RunSlopeAscent();

    int monotonicSteps = 0;
    for (std::size_t i = 61; i < trajectory.size(); ++i)
    {
        if (trajectory[i].position.y >= trajectory[i - 1].position.y - 0.001f)
            ++monotonicSteps;
    }
    EXPECT_GE(monotonicSteps, 55);
    EXPECT_GT(trajectory.back().position.y, trajectory[60].position.y + 1.0f);
    EXPECT_TRUE(trajectory.back().grounded);

    const auto baseline = LoadBaseline("movement_slope_ascent");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("movement_slope_ascent");
    const TraceDiff diff = CompareTraces(*baseline, trajectory, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trajectory);
}

TEST_F(MovementGolden, DISABLED_SaveBaselines)
{
    EXPECT_TRUE(SaveBaseline("movement_flat_walk", RunFlatWalk()));
    EXPECT_TRUE(SaveBaseline("movement_single_jump", RunSingleJump()));
    EXPECT_TRUE(SaveBaseline("movement_coyote_jump", RunCoyoteJump()));
    EXPECT_TRUE(SaveBaseline("movement_jump_buffer", RunJumpBuffer()));
    EXPECT_TRUE(SaveBaseline("movement_wall_collision", RunWallCollision()));
    EXPECT_TRUE(SaveBaseline("movement_ledge_climb", RunLedgeClimb()));
    EXPECT_TRUE(SaveBaseline("movement_slope_ascent", RunSlopeAscent()));
}
