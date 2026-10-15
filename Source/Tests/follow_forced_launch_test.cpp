#include "Game/Level/FollowCameraActor.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/ThirdPersonFollow.h"
#include "NSlib/Windows/Clock.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

namespace
{
    // 状態を試しが書く追われる物。勝手に出た突進の最中か・反動の状態かを毎フレーム渡す
    class LaunchTargetProbeActor final : public NS::SceneActor
    {
    public:
        // 接地と反動の状態を仮想カメラの口へ渡す
        void Feed(NS::ThirdPersonFollow& vcam) const
        {
            vcam.SetFollowMotion(true, NS::Vector3{});
            NS::FollowReboundDesc rebound{};
            rebound.rebounding = rebounding;
            rebound.forcedSlamming = forcedSlamming;
            rebound.slamDirection = NS::Vector3{0.0f, 0.0f, 1.0f};
            (void)vcam.SetFollowRebound(rebound);
        }

        bool rebounding = false;
        bool forcedSlamming = false;
    };

    // 頭から注視点までの水平の遅れ (m)
    float HorizontalLag(const NS::ThirdPersonFollow& vcam, const NS::SceneActor& target)
    {
        const NS::CameraPose pose = vcam.EvaluatePose(1.0f);
        const NS::Vector3 root = target.Root().Position();
        const float dx = pose.target.x - root.x;
        const float dz = pose.target.z - root.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    struct LaunchScene
    {
        NS::Scene scene;
        LaunchTargetProbeActor* target = nullptr;
        GL::Level::FollowCameraActor* camera = nullptr;

        void Load()
        {
            target = scene.SpawnObject<LaunchTargetProbeActor>("target");
            camera = scene.SpawnTransient<GL::Level::FollowCameraActor>();
            NS::ApplyJsonFields(camera->Vcam(),
                                nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}},
                                               {"強制発射の追う速さ", 4.0f},
                                               {"強制発射の遅れの上限", 3.0f}});
            // 止まった所から始める
            for (int frame = 0; frame < 30; ++frame)
            {
                Tick();
            }
        }

        // 追われる物の状態を渡してから、カメラを 1 フレーム進める
        void Tick()
        {
            target->Feed(camera->Vcam());
            camera->Update();
        }

        // 追われる物を 1 秒に 20 m で +Z へ進め、カメラを 1 フレーム進める
        void Step()
        {
            target->Root().ShiftPosition(NS::Vector3{0.0f, 0.0f, 20.0f * NS::OS::FrameTimer::FixedDelta()});
            Tick();
        }
    };
} // namespace

// 勝手に出た突進の間、注視点はその場に取り残されてから引っ張られ、遅れは上限を越えない
TEST(FollowForcedLaunch, LookIsLeftBehindThenPulledWithinTheCap)
{
    LaunchScene s;
    s.Load();
    s.target->forcedSlamming = true;
    s.Step();
    s.Step();
    const NS::ThirdPersonFollow& vcam = s.camera->Vcam();
    // バネは止まった所から動き出すので、出た最初のフレームはほぼ取り残される
    EXPECT_GT(HorizontalLag(vcam, *s.target), 0.5f);
    float maxLag = 0.0f;
    for (int frame = 0; frame < 40; ++frame)
    {
        s.Step();
        maxLag = std::max(maxLag, HorizontalLag(vcam, *s.target));
    }
    EXPECT_GT(maxLag, 2.0f);
    EXPECT_LE(maxLag, 3.0f + 0.0001f);
}

// 普通の突進では遅れない
TEST(FollowForcedLaunch, OrdinarySlamDoesNotLag)
{
    LaunchScene s;
    s.Load();
    for (int frame = 0; frame < 10; ++frame)
    {
        s.Step();
        EXPECT_LT(HorizontalLag(s.camera->Vcam(), *s.target), 0.0001f);
    }
}

// 遅れの区間の途中で当たって反動に入っても、注視点は跳ばずに今いる所から続く
TEST(FollowForcedLaunch, ReboundStartsFromTheLaggedLookWithoutAJump)
{
    LaunchScene s;
    s.Load();
    s.target->forcedSlamming = true;
    for (int frame = 0; frame < 12; ++frame)
    {
        s.Step();
    }
    const NS::ThirdPersonFollow& vcam = s.camera->Vcam();
    const NS::Vector3 before = vcam.EvaluatePose(1.0f).target;
    s.target->forcedSlamming = false;
    s.target->rebounding = true;
    s.Tick();
    const NS::Vector3 after = vcam.EvaluatePose(1.0f).target;
    EXPECT_LT((after - before).Length(), 0.5f);

    // 突進が終わって止まった時も跳ばずに寄せ戻す
    LaunchScene t;
    t.Load();
    t.target->forcedSlamming = true;
    for (int frame = 0; frame < 12; ++frame)
    {
        t.Step();
    }
    const NS::Vector3 held = t.camera->Vcam().EvaluatePose(1.0f).target;
    t.target->forcedSlamming = false;
    t.Tick();
    EXPECT_LT((t.camera->Vcam().EvaluatePose(1.0f).target - held).Length(), 0.5f);
}
