#include "Editor/EditorActors.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/ThirdPersonFollow.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "Tests/TestSceneSetup.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

// 真ん中の反動の間は、注視点に相手を重みで入れ、二人が離れた分だけ引く。相手が見送りの距離より遠いと自機だけを追う

namespace
{
    // 反動の状態と、画面に残す相手を試しが書く追われる物。動かない
    class PartnerTargetProbeActor final : public NS::SceneActor
    {
    public:
        // 空中と反動の状態を仮想カメラの口へ渡す
        void Feed(GL::Level::ThirdPersonFollow* vcam) const
        {
            NS_ASSERT(Game, vcam != nullptr, "Feed: 渡したアクターか部品が無い");
            vcam->SetFollowMotion(false, NS::Vector3{});
            GL::Level::FollowReboundDesc rebound{};
            rebound.rebounding = true;
            rebound.slamDirection = NS::Vector3{0.0f, 0.0f, 1.0f};
            rebound.partnerPosition = partner;
            rebound.pullBack = pullBack;
            (void)vcam->SetFollowRebound(rebound);
        }

        std::optional<NS::Vector3> partner;
        bool pullBack = true;
    };

    struct PartnerScene
    {
        NS::Scene scene;
        PartnerTargetProbeActor* target = nullptr;
        GL::Level::FollowCameraActor* camera = nullptr;

        explicit PartnerScene(const std::optional<NS::Vector3>& partner, bool pullBack = true)
        {
            target = scene.SpawnActor<PartnerTargetProbeActor>("target");
            target->partner = partner;
            target->pullBack = pullBack;
            camera = scene.SpawnActor<GL::Level::FollowCameraActor>("FollowCameraActor");
            NS::ApplyJsonFields(&camera->Vcam(),
                                nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}},
                                               {"反動の間に相手を収める重み", 0.3f},
                                               {"相手を見送る距離", 30.0f},
                                               {"引き始めの二人の距離", 3.0f},
                                               {"二人の距離 1 m あたりに引く距離", 0.3f},
                                               {"相手を収める引きの上限", 4.0f},
                                               {"相手を収めるばねの半分の秒", 0.15f}});
            // 欄を書いた後に、エディタと同じく相手を取得し直す
            camera->ResolveActorRefs();
            // 反動の間は遅れて追う形のまま 3 秒回し、ばねを落ち着かせる
            for (int frame = 0; frame < 180; ++frame)
            {
                target->Feed(&camera->Vcam());
                scene.GetCameraManager()->OnTick();
            }
        }

        [[nodiscard]] NS::CameraPose Pose() const { return camera->Vcam().EvaluatePose(1.0f); }
    };

    float DistanceOf(const NS::CameraPose& pose)
    {
        return (pose.position - pose.target).Length();
    }
} // namespace

// 相手が 10 m 先なら重みは 0.3 × (1 − 10 ÷ 30) = 0.2。注視点は相手の側へ二人の差の 2 割だけ寄り、
// カメラは 0.3 × (二人の距離 − 3) だけ引く
TEST(FollowReboundPartner, LookLeansTowardThePartnerAndPullsBack)
{
    const PartnerScene alone(std::nullopt);
    const NS::CameraPose without = alone.Pose();
    const NS::Vector3 partner = without.target + NS::Vector3{10.0f, 0.0f, 0.0f};
    const PartnerScene with(partner);
    const NS::CameraPose pose = with.Pose();

    const NS::Vector3 lean = pose.target - without.target;
    EXPECT_NEAR(lean.x, 2.0f, 0.01f);
    EXPECT_NEAR(lean.y, 0.0f, 0.01f);
    EXPECT_NEAR(lean.z, 0.0f, 0.01f);
    EXPECT_NEAR(DistanceOf(pose) - DistanceOf(without), 0.3f * (10.0f - 3.0f), 0.01f);
}

// 引きは上限で止まる
TEST(FollowReboundPartner, PullBackStopsAtTheCap)
{
    const PartnerScene alone(std::nullopt);
    const NS::CameraPose without = alone.Pose();
    const PartnerScene with(without.target + NS::Vector3{0.0f, 0.0f, 25.0f});
    EXPECT_NEAR(DistanceOf(with.Pose()) - DistanceOf(without), 4.0f, 0.01f);
}

// 見送りの距離より遠い相手は重みが 0 で、自機だけを追う。引きも 0 へ戻る
TEST(FollowReboundPartner, PartnerBeyondTheReleaseDistanceIsLetGo)
{
    const PartnerScene alone(std::nullopt);
    const NS::CameraPose without = alone.Pose();
    const PartnerScene with(without.target + NS::Vector3{0.0f, 0.0f, 40.0f});
    const NS::CameraPose pose = with.Pose();
    EXPECT_NEAR((pose.target - without.target).Length(), 0.0f, 0.001f);
    EXPECT_NEAR(DistanceOf(pose), DistanceOf(without), 0.001f);
}

// 下げない反動 (外れ) は、反動になったフレームの距離のまま追う。下げる反動とは欄「反動の間に下げる距離」1 m だけ違う
TEST(FollowReboundPartner, ReboundWithoutPullBackKeepsTheDistance)
{
    const PartnerScene pulled(std::nullopt);
    const PartnerScene kept(std::nullopt, false);
    EXPECT_NEAR(DistanceOf(pulled.Pose()) - DistanceOf(kept.Pose()), 1.0f, 0.01f);
}
