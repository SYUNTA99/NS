#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Math/Math.h>
#include <Framework/Physics/PhysicsWorld.h>
#include <Framework/Scene/Components/CameraBrainComponent.h>
#include <Framework/Scene/Components/CameraComponent.h>
#include <Framework/Scene/Components/CharacterMovementComponent.h>
#include <Framework/Scene/Components/PlacedVirtualCamera.h>
#include <Framework/Scene/Components/ThirdPersonFollowComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Vector3;
    using NS::Scene::CameraBrainComponent;
    using NS::Scene::CameraComponent;
    using NS::Scene::CameraPose;
    using NS::Scene::CharacterMovementComponent;
    using NS::Scene::GameObject;
    using NS::Scene::PlacedVirtualCamera;
    using NS::Scene::ThirdPersonFollowComponent;

    constexpr float kFixedDt = 1.0f / 60.0f;

    /// 1 step ごとのカメラ姿勢。位置 / 注視点 / 画角でカメラの見えは一意に決まる
    struct CameraStepRecord
    {
        Vector3 position;
        Vector3 target;
        float fovY = 0.0f;
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
    /// 投入するため、1 bit でもカメラの動きが変われば必ず値が変わる
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

    /// ハッシュ不一致時の一次診断。実測ハッシュと 20 step ごとの要約を返す
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

    /// 実プレイヤー相当の移動体を床上へ立てて走らせ、追従カメラの姿勢を毎 step 記録する
    /// 加速で idle→run ズーム、ジャンプで jump ズーム、停止で idle へ戻る自動距離の全遷移と
    /// spring の収束、render 補間 (alpha=0.5) の姿勢を 1 本の軌跡に焼く
    std::vector<CameraStepRecord> RunFollowWalkJump()
    {
        GameObject player;
        NS::Physics::PhysicsWorld world;
        world.AddAabb(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{64.0f, 0.5f, 8.0f}});
        auto& movement = *player.AddComponent<CharacterMovementComponent>();
        player.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
        world.BuildBroadphase();
        movement.SetPhysicsWorld(&world);
        movement.SetDebugDrawEnabled(false);

        GameObject rig;
        auto& follow = *rig.AddComponent<ThirdPersonFollowComponent>(&player.Root());
        follow.SetMovement(&movement);

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
            follow.OnUpdate();

            // fixed step の確定姿勢と render 補間の中間姿勢の両方を焼く
            trajectory.push_back(RecordPose(follow.EvaluatePose(1.0f)));
            trajectory.push_back(RecordPose(follow.EvaluatePose(0.5f)));
        }
        return trajectory;
    }

    /// 追従カメラで歩くプレイヤーが据え置きカメラのトリガへ進入 → 滞在 → 退出する
    /// Brain の優先度選択・進入時の 0.3 秒ブレンド・lookAtPlayer の追視・退出時の
    /// 追従カメラへの戻りブレンドを、実カメラへ書かれた姿勢として記録する
    std::vector<CameraStepRecord> RunAreaCameraBlend()
    {
        GameObject host;
        auto* cam = host.AddComponent<CameraComponent>();
        auto* brain = host.AddComponent<CameraBrainComponent>();
        host.OnStart();
        brain->SetBlendDuration(0.3f);

        GameObject player;
        player.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});

        GameObject rig;
        auto& follow = *rig.AddComponent<ThirdPersonFollowComponent>(&player.Root());

        GameObject areaHost;
        auto& placed = *areaHost.AddComponent<PlacedVirtualCamera>();
        placed.SetView(Vector3{8.0f, 4.0f, -6.0f}, Vector3{8.0f, 0.0f, 0.0f});
        placed.SetTrigger(Vector3{8.0f, 1.0f, 0.0f}, Vector3{2.0f, 1.5f, 2.0f});
        placed.SetLookAtPlayer(true);
        placed.SetVcamPriority(20);
        placed.SetFovY(NS::Math::ToRadians(NS::Math::Degrees{50.0f}));

        brain->AddVirtualCamera(&follow);
        brain->AddVirtualCamera(&placed);

        std::vector<CameraStepRecord> trajectory;
        for (int i = 0; i < 260; ++i)
        {
            player.Root().Snapshot();
            // 3 u/s で X+ へ歩かせる。トリガ x 6..10 へは i=120 で入り i=200 で抜ける
            const float x = 0.05f * static_cast<float>(i + 1);
            player.Root().SetPosition(Vector3{x, 1.0f, 0.0f});

            placed.UpdateActivation(player.Root().Position());
            follow.OnUpdate();
            brain->OnUpdate();
            brain->Evaluate(1.0f);

            trajectory.push_back(CameraStepRecord{cam->Position(), cam->Target(), cam->FovY().value});
        }
        return trajectory;
    }

    // 基準ハッシュ。カメラの手触りに触る改修の前後で軌跡の bit 一致を守る門番で、
    // 意図してカメラの感触を変えた時だけ実測値で更新する
    constexpr uint64_t kFollowWalkJumpGolden = 0x6C68A644E4D28AAEULL;
    constexpr uint64_t kAreaCameraBlendGolden = 0xDF21CBDB3D18F8D1ULL;
} // namespace

class CameraGolden : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kFixedDt); }
};

/// ハッシュ方式の前提検証: 同一 build 内の 2 run が bit 一致すること
TEST_F(CameraGolden, HashIsStableAcrossTwoRuns)
{
    EXPECT_EQ(HashTrajectory(RunFollowWalkJump()), HashTrajectory(RunFollowWalkJump()));
    EXPECT_EQ(HashTrajectory(RunAreaCameraBlend()), HashTrajectory(RunAreaCameraBlend()));
}

TEST_F(CameraGolden, FollowWalkJumpMatchesGoldenTrace)
{
    const auto trajectory = RunFollowWalkJump();

    // 自動ズームの全遷移が通っていることの表面検証。距離 = |カメラ - 注視点|
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
    EXPECT_EQ(hash, kFollowWalkJumpGolden) << DescribeTrajectory(trajectory, hash);
}

TEST_F(CameraGolden, AreaCameraBlendMatchesGoldenTrace)
{
    const auto trajectory = RunAreaCameraBlend();

    // トリガ滞在が安定した頃の i=180 すなわち x 9.05 では据え置き位置へ完全に到達している
    const CameraStepRecord& inside = trajectory[180];
    EXPECT_NEAR(inside.position.x, 8.0f, 0.05f) << "滞在中に据え置きカメラ位置へ到達していない";
    EXPECT_NEAR(inside.position.y, 4.0f, 0.05f);
    EXPECT_NEAR(inside.target.x, 9.05f, 0.1f) << "lookAtPlayer が注視点をプレイヤーへ向けていない";

    // 退出してブレンドが終わった末尾は追従カメラへ戻っている
    const CameraStepRecord& last = trajectory.back();
    EXPECT_GT(last.position.z, -6.0f + 1.0f) << "退出後も据え置きカメラに留まっている";
    EXPECT_NEAR(last.target.y, 1.0f + 1.2f, 0.2f) << "退出後の注視点が頭高さに戻っていない";

    const uint64_t hash = HashTrajectory(trajectory);
    EXPECT_EQ(hash, kAreaCameraBlendGolden) << DescribeTrajectory(trajectory, hash);
}
