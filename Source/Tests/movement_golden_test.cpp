#include <algorithm>
#include <bit>
#include <cstdint>
#include <gtest/gtest.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Object::CharacterMovementComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    /// 1 fixed step ごとの表面状態。coyote / jump buffer 等の内部 timer は
    /// 必ず位置・速度・接地の変化として表面に出るため、この 3 つだけで足りる
    struct StepRecord
    {
        Vector3 position;
        Vector3 velocity;
        bool grounded = false;
    };

    /// FNV-1a 64bit へ 4 byte を畳み込む
    uint64_t FoldFnv1a(uint64_t hash, uint32_t value) noexcept
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            hash ^= (value >> shift) & 0xFFu;
            hash *= 0x100000001B3ULL;
        }
        return hash;
    }

    /// 軌跡全 step を 1 つのハッシュへ畳み込む。float は bit 表現のまま
    /// 投入するため、1 bit でも挙動が変われば必ず値が変わる
    uint64_t HashTrajectory(const std::vector<StepRecord>& trajectory) noexcept
    {
        uint64_t hash = 0xCBF29CE484222325ULL;
        for (const StepRecord& s : trajectory)
        {
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.position.x));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.position.y));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.position.z));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.velocity.x));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.velocity.y));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.velocity.z));
            uint32_t groundedBit = 0u;
            if (s.grounded)
                groundedBit = 1u;
            hash = FoldFnv1a(hash, groundedBit);
        }
        return hash;
    }

    /// ハッシュ不一致時の一次診断。実測ハッシュと 10 step ごとの要約を返す
    std::string DescribeTrajectory(const std::vector<StepRecord>& trajectory, uint64_t hash)
    {
        std::ostringstream out;
        out << "actual hash = 0x" << std::hex << hash << std::dec << "\n";
        for (size_t i = 0; i < trajectory.size(); i += 10)
        {
            const StepRecord& s = trajectory[i];
            out << "step " << i << ": pos " << s.position.x << " " << s.position.y << " " << s.position.z << " / vel "
                << s.velocity.x << " " << s.velocity.y << " " << s.velocity.z << " / grounded " << s.grounded << "\n";
        }
        return out.str();
    }

    float MaxHeight(const std::vector<StepRecord>& trajectory) noexcept
    {
        float peak = -1000.0f;
        for (const StepRecord& s : trajectory)
            peak = std::max(s.position.y, peak);
        return peak;
    }

    /// ジャンプ発動の検出。impulse 12 の上向き速度は自由落下では絶対に出ない
    bool HasUpwardBurst(const std::vector<StepRecord>& trajectory) noexcept
    {
        for (const StepRecord& s : trajectory)
        {
            if (s.velocity.y > 5.0f)
                return true;
        }
        return false;
    }

    /// owner に移動 component を載せ物理 world を繋ぐ。開始位置は空中に取り、
    /// 数 step の自然落下で着地させる。床上へ直置きすると capsule が床へめり込み、
    /// 衝突解決が移動を丸ごと拒否して位置が固定されてしまう
    CharacterMovementComponent& SetUpMovement(GameObject& owner,
                                              NS::Physics::PhysicsWorld& world,
                                              const Vector3& startPosition)
    {
        auto& movement = *owner.AddComponent<CharacterMovementComponent>();
        owner.Root().SetPosition(startPosition);
        world.BuildBroadphase();
        movement.SetPhysicsWorld(&world);
        movement.SetDebugDrawEnabled(false);
        return movement;
    }

    StepRecord Record(GameObject& owner, const CharacterMovementComponent& movement)
    {
        return StepRecord{owner.Root().Position(), movement.Velocity(), movement.IsGrounded()};
    }

    /// 平地を X+ へ全開で走り、90 step 目から入力を切って停止する
    /// 加速の立ち上がり・最大速度巡航・減速の 3 経路を通す
    std::vector<StepRecord> RunFlatWalk()
    {
        GameObject owner;
        NS::Physics::PhysicsWorld world;
        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{16.0f, 0.5f, 8.0f}});
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

    /// 走りながらジャンプ 1 回。20 step 保持してから離すことで
    /// 上昇の弱い重力・離し減速・頂点の重力緩和・落下の強い重力を全て通す
    /// 床は 3 秒間の全力走行で走り抜けない長さにする
    std::vector<StepRecord> RunSingleJump()
    {
        GameObject owner;
        NS::Physics::PhysicsWorld world;
        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{32.0f, 0.5f, 8.0f}});
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

    /// 短い床を走り抜けて踏み外し、その次の step で jump press する
    /// press 時点で空中 1 step ぶんの時間が経過しており、coyote 窓の内側を踏む
    std::vector<StepRecord> RunCoyoteJump()
    {
        GameObject owner;
        NS::Physics::PhysicsWorld world;
        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{2.0f, 0.5f, 8.0f}});
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

    /// 高所から自由落下し、着地前の空中で jump press を先行入力する
    /// 着地の瞬間に buffer が消費されて即ジャンプする経路を固定する
    std::vector<StepRecord> RunJumpBuffer()
    {
        GameObject owner;
        NS::Physics::PhysicsWorld world;
        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{8.0f, 0.5f, 8.0f}});
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

    /// 壁へ向かって走り続け、衝突解決で壁面手前に止まる押し戻しを固定する
    std::vector<StepRecord> RunWallCollision()
    {
        GameObject owner;
        NS::Physics::PhysicsWorld world;
        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{8.0f, 0.5f, 8.0f}});
        world.AddAABB(AABB{Vector3{6.0f, 1.5f, 0.0f}, Vector3{0.5f, 2.0f, 8.0f}});
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

    // 基準ハッシュ。手触りに触る改修の前後で軌跡の bit 一致を検証する物で、
    // 意図して手触りを変えた時だけ実測値で更新する
    constexpr uint64_t k_FlatWalkGolden = 0x4FA4FA4FCFB0F728ULL;
    constexpr uint64_t k_SingleJumpGolden = 0xC15864A95E5EDFCDULL;
    constexpr uint64_t k_CoyoteJumpGolden = 0xE363FC53420CB70DULL;
    constexpr uint64_t k_JumpBufferGolden = 0xFC63ACD2279A8321ULL;
    constexpr uint64_t k_WallCollisionGolden = 0xE38F47F9986195ACULL;
} // namespace

class MovementGolden : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

/// ハッシュ方式の前提として、同じビルドで 2 回走らせた結果が bit 一致すること
TEST_F(MovementGolden, HashIsStableAcrossTwoRuns)
{
    EXPECT_EQ(HashTrajectory(RunSingleJump()), HashTrajectory(RunSingleJump()));
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

    const uint64_t hash = HashTrajectory(trajectory);
    EXPECT_EQ(hash, k_FlatWalkGolden) << DescribeTrajectory(trajectory, hash);
}

TEST_F(MovementGolden, SingleJumpMatchesGoldenTrace)
{
    const auto trajectory = RunSingleJump();

    EXPECT_GT(MaxHeight(trajectory), 2.0f) << "ジャンプ頂点が低すぎる";
    EXPECT_LT(MaxHeight(trajectory), 6.0f) << "ジャンプ頂点が高すぎる";
    EXPECT_TRUE(trajectory.back().grounded) << "着地して終わっていない";

    const uint64_t hash = HashTrajectory(trajectory);
    EXPECT_EQ(hash, k_SingleJumpGolden) << DescribeTrajectory(trajectory, hash);
}

TEST_F(MovementGolden, CoyoteJumpMatchesGoldenTrace)
{
    const auto trajectory = RunCoyoteJump();

    EXPECT_TRUE(HasUpwardBurst(trajectory)) << "踏み外し後の猶予ジャンプが発動していない";

    const uint64_t hash = HashTrajectory(trajectory);
    EXPECT_EQ(hash, k_CoyoteJumpGolden) << DescribeTrajectory(trajectory, hash);
}

TEST_F(MovementGolden, JumpBufferMatchesGoldenTrace)
{
    const auto trajectory = RunJumpBuffer();

    EXPECT_TRUE(HasUpwardBurst(trajectory)) << "着地時に先行入力ジャンプが発動していない";

    const uint64_t hash = HashTrajectory(trajectory);
    EXPECT_EQ(hash, k_JumpBufferGolden) << DescribeTrajectory(trajectory, hash);
}

TEST_F(MovementGolden, WallCollisionMatchesGoldenTrace)
{
    const auto trajectory = RunWallCollision();

    EXPECT_LT(trajectory.back().position.x, 5.5f) << "壁にめり込んでいる";
    EXPECT_GT(trajectory.back().position.x, 4.0f) << "壁のはるか手前で止まっている";
    EXPECT_LT(trajectory.back().velocity.x, 0.5f) << "壁に当たり続けているのに速度が残っている";

    const uint64_t hash = HashTrajectory(trajectory);
    EXPECT_EQ(hash, k_WallCollisionGolden) << DescribeTrajectory(trajectory, hash);
}
