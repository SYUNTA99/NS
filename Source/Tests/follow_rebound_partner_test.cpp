#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
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
        void Feed(NS::ThirdPersonFollow& vcam) const
        {
            vcam.SetFollowMotion(false, NS::Vector3{});
            NS::FollowReboundDesc rebound{};
            rebound.rebounding = true;
            rebound.slamDirection = NS::Vector3{0.0f, 0.0f, 1.0f};
            rebound.partnerPosition = partner;
            rebound.pullBack = pullBack;
            (void)vcam.SetFollowRebound(rebound);
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
            target = scene.SpawnObject<PartnerTargetProbeActor>("target");
            target->partner = partner;
            target->pullBack = pullBack;
            camera = scene.SpawnTransient<GL::Level::FollowCameraActor>();
            NS::ApplyJsonFields(camera->Vcam(),
                                nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}},
                                               {"反動の間に相手を収める重み", 0.3f},
                                               {"相手を見送る距離", 30.0f},
                                               {"引き始めの二人の距離", 3.0f},
                                               {"二人の距離 1 m あたりに引く距離", 0.3f},
                                               {"相手を収める引きの上限", 4.0f},
                                               {"相手を収めるばねの半分の秒", 0.15f}});
            // 反動の間は遅れて追う形のまま 3 秒回し、ばねを落ち着かせる
            for (int frame = 0; frame < 180; ++frame)
            {
                target->Feed(camera->Vcam());
                camera->Update();
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

namespace
{
    // 床の上に自機 (id 1) と、その前に置物 (id 2) を置く
    PlayerActor* PlacePartnerScene(NS::Scene& scene, float rockX)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::MakeObjectJson();
        NS::SetObjectJsonClass(player, "PlayerActor");
        NS::SetObjectJsonId(player, 1);
        NS::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonClass(rock, "PropActor");
        NS::SetObjectJsonId(rock, 2);
        NS::SetObjectPosition(rock, NS::Vector3{rockX, 0.5f, 0.6f});
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Cast<PlayerActor>(scene.Objects().FindByObjectId(1));
    }
} // namespace

// 自機は真ん中の反動の間だけ、飛ばした相手の今の位置をカメラへ渡す。外れの反動は渡さず、いつもどおり自機を追い、後ろへも下げない
TEST(FollowReboundPartner, PlayerPassesTheLaunchedTargetOnlyForACenterRebound)
{
    for (const float rockX : {0.0f, 0.75f})
    {
        SCOPED_TRACE(rockX);
        NS::Scene scene;
        PlayerActor* player = PlacePartnerScene(scene, rockX);
        ASSERT_NE(player, nullptr);
        GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
        ASSERT_NE(rock, nullptr);
        player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        int checked = 0;
        int held = 0;
        for (int frame = 0; frame < 400 && checked < 5; ++frame)
        {
            player->Update(false);
            rock->Update();
            // 体当たりと止めの間は、カメラに距離と溜めの締めを保たせる
            if (player->IsBodySlamming() || player->Resolver().Answer().holdingPlayer)
            {
                EXPECT_TRUE(player->GetCameraTargetState().framingHeld);
                ++held;
            }
            if (!player->IsRebounding())
            {
                continue;
            }
            const GL::Player::CameraTargetState state = player->GetCameraTargetState();
            // 反動の間はもう保たない。保つのは体当たりから止めの明けまで
            EXPECT_FALSE(state.framingHeld);
            if (player->Resolver().Answer().lastImpact.tier == GL::Level::HitTier::Center)
            {
                ASSERT_TRUE(state.rebound.partnerPosition.has_value());
                EXPECT_TRUE(state.rebound.partnerPosition.value() == rock->Root().Position());
                EXPECT_GT(state.rebound.slamDirection.Length(), 0.0f);
                EXPECT_TRUE(state.rebound.pullBack);
            }
            else
            {
                EXPECT_FALSE(state.rebound.partnerPosition.has_value());
                // 外れはカメラを突進の向きへ回り込ませない。どちらへ外したかを、自機が画面の中で逸れる幅で見せる
                EXPECT_FLOAT_EQ(state.rebound.slamDirection.Length(), 0.0f);
                // 外れはカメラを後ろへ下げない。相手はほとんど飛ばないので、二人を収めるために引く物が無い
                EXPECT_FALSE(state.rebound.pullBack);
            }
            ++checked;
        }
        EXPECT_EQ(checked, 5);
        EXPECT_GT(held, 0);
        if (rockX == 0.0f)
        {
            EXPECT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Center);
        }
        else
        {
            EXPECT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Wide);
        }
    }
}
