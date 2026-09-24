#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManager.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CameraBrain.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsScene.h>

#include "entity_test_stage.h"
#include "jolt_test_scene.h"
#include "tuning_field_access.h"
#include <bit>
#include <cstdint>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Obj::CameraBrain;
    using NS::Obj::CameraComponent;
    using NS::Obj::CameraPose;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManager;
    using NS::Obj::GameObject;
    using NS::Obj::ObjectIdAccess;
    using NS::Obj::ThirdPersonFollow;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr std::uint32_t k_PlayerId = 1u;

    //! 1 step ごとのカメラ姿勢。見えはこの 3 つで決まる
    struct CameraStepRecord
    {
        Vector3 position;
        Vector3 target;
        float fovY = 0.0f;
    };

    //! FNV-1a 64bit へ 4 byte を畳み込む
    uint64_t FoldFnv1a(uint64_t hash, uint32_t value) noexcept
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            hash ^= (value >> shift) & 0xFFu;
            hash *= 0x100000001B3ULL;
        }
        return hash;
    }

    //! 軌跡全 step を 1 つのハッシュへ畳み込む。float は bit 表現のまま入れるので 1 bit の差も逃さない
    uint64_t HashTrajectory(const std::vector<CameraStepRecord>& trajectory) noexcept
    {
        uint64_t hash = 0xCBF29CE484222325ULL;
        for (const CameraStepRecord& s : trajectory)
        {
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.position.x));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.position.y));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.position.z));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.target.x));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.target.y));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.target.z));
            hash = FoldFnv1a(hash, std::bit_cast<uint32_t>(s.fovY));
        }
        return hash;
    }

    //! ハッシュ不一致時の手がかり用。実測ハッシュと 20 step ごとの要約を返す
    std::string DescribeTrajectory(const std::vector<CameraStepRecord>& trajectory, uint64_t hash)
    {
        std::ostringstream out;
        out << "actual hash = 0x" << std::hex << hash << std::dec << "\n";
        for (size_t i = 0; i < trajectory.size(); i += 20)
        {
            const CameraStepRecord& s = trajectory[i];
            out << "step " << i << ": pos " << s.position.x << " " << s.position.y << " " << s.position.z
                << " / target " << s.target.x << " " << s.target.y << " " << s.target.z << " / fov " << s.fovY << "\n";
        }
        return out.str();
    }

    float DistanceBetween(const Vector3& a, const Vector3& b) noexcept
    {
        const Vector3 d{a.x - b.x, a.y - b.y, a.z - b.z};
        return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    }

    CameraStepRecord RecordPose(const CameraPose& pose)
    {
        return CameraStepRecord{pose.position, pose.target, pose.fovY.value};
    }

    //! プレイヤー相当を床上で走らせ、追従カメラの姿勢を毎 step 記録する
    //! 加速・ジャンプ・停止の自動ズーム全遷移と render 補間 (alpha=0.5) を 1 本の軌跡に記録する
    std::vector<CameraStepRecord> RunFollowWalkJump()
    {
        NsTest::EntityStage stage;
        GameObject& player = stage.owner;
        NS::Phys::PhysicsScene& physics = stage.physics;
        NsTest::AddBox(physics, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{64.0f, 0.5f, 8.0f}});
        player.AddComponent<PlayerStateManager>();
        auto& movement = *player.AddComponent<PlayerComponent>();
        player.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
        physics.OptimizeBroadPhase();
        player.OnStart();
        ObjectIdAccess::SetId(player, k_PlayerId);

        GameObject& rig = *stage.scene.SpawnTransient<GameObject>();
        auto& follow = *rig.AddComponent<ThirdPersonFollow>();
        NsTest::WriteObjectRefField(follow, "追従対象", k_PlayerId);
        // 生成直後は休止なのでテスト側で有効化する
        follow.SetActive(true);

        std::vector<CameraStepRecord> trajectory;
        for (int i = 0; i < 210; ++i)
        {
            player.Root().Snapshot();
            if (i < 150)
                movement.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            else
                movement.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
            if (i == 60)
                movement.SetJumpPressed();
            movement.SetJumpHeld(i >= 60 && i < 80);
            movement.OnUpdate();
            follow.SetFollowMotion(movement.IsGrounded(), movement.Velocity());
            follow.OnUpdate();

            // fixed step の確定姿勢と render 補間の中間姿勢の両方を記録する
            trajectory.push_back(RecordPose(follow.EvaluatePose(1.0f)));
            trajectory.push_back(RecordPose(follow.EvaluatePose(0.5f)));
        }
        return trajectory;
    }

    // 基準ハッシュ。カメラか自機の動きを意図して変えた時だけ実測値で更新する
    constexpr uint64_t k_FollowWalkJumpGolden = 0x6A49F2A9A3EA6903ULL;
} // namespace

class CameraGolden : public ::testing::Test
{
protected:
    void SetUp() override { NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt); }
};

//! 同一 build 内の 2 run が bit 一致する前提を確かめる
TEST_F(CameraGolden, HashIsStableAcrossTwoRuns)
{
    EXPECT_EQ(HashTrajectory(RunFollowWalkJump()), HashTrajectory(RunFollowWalkJump()));
}

TEST_F(CameraGolden, FollowWalkJumpMatchesGoldenTrace)
{
    const auto trajectory = RunFollowWalkJump();

    // 自動ズームの全遷移が通ったかを距離 = |カメラ - 注視点| でざっくり確かめる
    float minDistance = 1000.0f;
    float maxDistance = 0.0f;
    for (const CameraStepRecord& s : trajectory)
    {
        const float d = DistanceBetween(s.position, s.target);
        if (d < minDistance)
            minDistance = d;
        if (d > maxDistance)
            maxDistance = d;
    }
    EXPECT_LT(minDistance, 5.5f) << "idle 距離へ寄っていない";
    EXPECT_GT(maxDistance, 6.5f) << "ジャンプ距離へ引いていない";
    EXPECT_NEAR(trajectory.back().target.y, 1.0f + 1.2f, 0.2f) << "注視点が頭高さに載っていない";

    const uint64_t hash = HashTrajectory(trajectory);
    EXPECT_EQ(hash, k_FollowWalkJumpGolden) << DescribeTrajectory(trajectory, hash);
}
