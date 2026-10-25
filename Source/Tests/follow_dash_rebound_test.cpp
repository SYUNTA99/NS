#include "Game/Level/FollowCameraActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <gtest/gtest.h>

#include <cmath>

// 速く動く間のカメラ: 縦の向きで上下の帯をずらす・視野角を広げる・自機との間の地形で縮める

namespace
{
    // 試しが書いた反動の状態を仮想カメラへ渡す、追われる物
    class DashTargetProbeActor final : public NS::SceneActor
    {
    public:
        void Feed(NS::ThirdPersonFollow* vcam) const
        {
            NS_ASSERT(Game, vcam != nullptr, "Feed: 渡したアクターか部品が無い");
            vcam->SetFollowMotion(false, NS::Vector3{});
            (void)vcam->SetFollowRebound(rebound);
        }

        NS::FollowReboundDesc rebound{};
    };

    struct DashScene
    {
        NS::Scene scene;
        DashTargetProbeActor* target = nullptr;
        GL::Level::FollowCameraActor* camera = nullptr;

        DashScene()
        {
            target = scene.SpawnActor<DashTargetProbeActor>("target");
            camera = scene.SpawnActor<GL::Level::FollowCameraActor>("FollowCameraActor");
            NS::ApplyJsonFields(&camera->Vcam(), nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}}});
            camera->ResolveActorRefs();
        }

        void Step(int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                target->Feed(&camera->Vcam());
                camera->Update();
            }
        }

        [[nodiscard]] NS::CameraPose Pose() const { return camera->Vcam().EvaluatePose(1.0f); }
    };

    // 反動の間に追う相手を真上へ動かし、最後のフレームの姿を返す
    NS::CameraPose RiseDuringRebound(float verticalLean)
    {
        DashScene dash;
        dash.Step(30);
        dash.target->rebound.rebounding = true;
        dash.target->rebound.holdLook = false;
        dash.target->rebound.pullBack = false;
        dash.target->rebound.verticalLean = verticalLean;
        for (int frame = 0; frame < 60; ++frame)
        {
            dash.target->Root().SetPosition(NS::Vector3{0.0f, 0.1f * static_cast<float>(frame), 0.0f});
            dash.Step(1);
        }
        return dash.Pose();
    }

    float DistanceOf(const NS::CameraPose& pose)
    {
        return (pose.position - pose.target).Length();
    }
} // namespace

// 上へ返る反動は、追う相手を画面の下寄りに留める。注視点が早く上がり、上に空きができる
TEST(FollowDashRebound, UpwardReboundKeepsTheTargetLowOnTheScreen)
{
    const NS::CameraPose level = RiseDuringRebound(0.0f);
    const NS::CameraPose upward = RiseDuringRebound(1.0f);
    EXPECT_GT(upward.target.y, level.target.y + 0.5f);
}

// 速い間は視野角を欄の角度だけ広げ、終わると基準へ戻す
TEST(FollowDashRebound, WidensTheViewWhileFastAndReturnsAfter)
{
    DashScene dash;
    dash.Step(10);
    const float base = dash.Pose().fovY.value;
    const float widen = NS::DegreesToRadians(
        NS::SerializeClassObjFields(&dash.camera->Vcam()).at("速い間に広げる視野角").get<float>());
    ASSERT_GT(widen, 0.0f);

    dash.target->rebound.widenView = true;
    dash.Step(180);
    EXPECT_NEAR(dash.Pose().fovY.value, base + widen, 1.0e-3f);

    dash.target->rebound.widenView = false;
    dash.Step(180);
    EXPECT_NEAR(dash.Pose().fovY.value, base, 1.0e-3f);
}

// 反動でカメラへ向かって飛ぶ間も、注視点の遅れでカメラが自機へ寄らない。寄るのは欄の上限まで
TEST(FollowDashRebound, ReboundTowardTheCameraKeepsTheDistance)
{
    DashScene dash;
    dash.Step(120);
    const NS::CameraPose before = dash.Pose();
    NS::Vector3 toCamera = before.position - before.target;
    toCamera.y = 0.0f;
    const float startGap = toCamera.Length();
    toCamera.Normalize();
    const float cap = NS::SerializeClassObjFields(&dash.camera->Vcam()).at("カメラへ向かう遅れの上限").get<float>();
    ASSERT_GE(cap, 0.0f);

    dash.target->rebound.rebounding = true;
    dash.target->rebound.holdLook = false;
    dash.target->rebound.pullBack = false;
    // 地形の反動の速さ 14 m/s で、カメラへ真っすぐ 0.5 秒飛ぶ
    float smallestGap = startGap;
    for (int frame = 1; frame <= 30; ++frame)
    {
        dash.target->Root().SetPosition(toCamera * (14.0f / 60.0f) * static_cast<float>(frame));
        dash.Step(1);
        const NS::CameraPose pose = dash.Pose();
        NS::Vector3 gap = pose.position - dash.target->Root().Position();
        gap.y = 0.0f;
        smallestGap = std::min(smallestGap, gap.Length());
    }
    EXPECT_GT(smallestGap, startGap - cap - 0.05f);
}

// 自機とカメラの間に地形があれば、地形の手前まで縮める。地形が無くなれば元の距離へ戻す
TEST(FollowDashRebound, TerrainBetweenShortensTheDistance)
{
    DashScene dash;
    dash.Step(120);
    const NS::CameraPose open = dash.Pose();
    const float distance = DistanceOf(open);
    ASSERT_GT(distance, 3.0f);

    // 注視点から後ろへ 2 m の所に、面が向かい合う壁を置く
    const NS::Vector3 back = open.position - open.target;
    NS::Vector3 backward = back;
    backward.Normalize();
    NS::OBB wall;
    wall.center = open.target + backward * 2.5f;
    wall.halfExtentX = 10.0f;
    wall.halfExtentY = 10.0f;
    wall.halfExtentZ = 0.5f;
    const JPH::BodyID body = dash.scene.Physics().AddBox(wall, NS::Phys::ObjectLayers::Terrain);
    dash.Step(1);
    const float blocked = DistanceOf(dash.Pose());
    EXPECT_LT(blocked, 2.0f);
    EXPECT_GT(blocked, 1.0f);

    dash.scene.Physics().RemoveBody(body);
    dash.Step(180);
    EXPECT_NEAR(DistanceOf(dash.Pose()), distance, 1.0e-2f);
}

// 壁の前で真上へ返る間、帯のずらしで注視点が壁の中へ入っても、自機と壁の間が空いていれば縮めない
TEST(FollowDashRebound, LookInsideTheWallDoesNotCollapseTheDistance)
{
    DashScene dash;
    dash.Step(120);
    const float distance = DistanceOf(dash.Pose());
    ASSERT_GT(distance, 3.0f);

    // 追う相手の 0.6 m 奥に、カメラへ面を向けた壁を置く
    NS::OBB wall;
    wall.center = NS::Vector3{0.0f, 0.0f, 1.6f};
    wall.halfExtentX = 10.0f;
    wall.halfExtentY = 20.0f;
    wall.halfExtentZ = 1.0f;
    (void)dash.scene.Physics().AddBox(wall, NS::Phys::ObjectLayers::Terrain);

    dash.target->rebound.rebounding = true;
    dash.target->rebound.holdLook = false;
    dash.target->rebound.pullBack = false;
    dash.target->rebound.verticalLean = 1.0f;
    float shortest = distance;
    for (int frame = 1; frame <= 40; ++frame)
    {
        dash.target->Root().SetPosition(NS::Vector3{0.0f, 0.25f * static_cast<float>(frame), 0.0f});
        dash.Step(1);
        shortest = std::min(shortest, DistanceOf(dash.Pose()));
    }
    EXPECT_GT(shortest, distance * 0.5f);
}

// 天井の下で頭の高さが天井の中に入っても、自機と天井の下の間が空いていれば縮めない
TEST(FollowDashRebound, HeadInsideTheCeilingDoesNotCollapseTheDistance)
{
    DashScene dash;
    dash.Step(120);
    const NS::CameraPose open = dash.Pose();
    const float distance = DistanceOf(open);
    ASSERT_GT(distance, 3.0f);
    NS::Vector3 back = open.position - open.target;
    back.y = 0.0f;
    back.Normalize();
    ASSERT_GT(std::fabs(back.z), 0.99f);

    // 頭の高さを中に含む薄い天井。自機の真上から前へ広がり、カメラの側へは 0.3 m だけ出る
    const float headY = open.target.y;
    NS::OBB ceiling;
    ceiling.center = NS::Vector3{0.0f, headY + 0.1f, 0.0f} - back * 0.6f;
    ceiling.halfExtentX = 3.0f;
    ceiling.halfExtentY = 0.5f;
    ceiling.halfExtentZ = 0.9f;
    (void)dash.scene.Physics().AddBox(ceiling, NS::Phys::ObjectLayers::Terrain);

    float shortest = distance;
    for (int frame = 0; frame < 10; ++frame)
    {
        dash.Step(1);
        shortest = std::min(shortest, DistanceOf(dash.Pose()));
    }
    EXPECT_GT(shortest, distance * 0.8f);
}

// 上へ返って天井の下へ入る間、天井がカメラと自機の間に無ければ縮めない
TEST(FollowDashRebound, CeilingAboveTheViewLineDoesNotShortenTheDistance)
{
    DashScene dash;
    dash.Step(120);
    const NS::CameraPose open = dash.Pose();
    const float distance = DistanceOf(open);
    NS::Vector3 back = open.position - open.target;
    back.y = 0.0f;
    back.Normalize();
    ASSERT_GT(std::fabs(back.z), 0.99f);
    const float headAboveRoot = open.target.y - dash.target->Root().Position().y;

    // 根が 4.5 m まで上がった時の頭の 0.2 m 上に下の面がある天井。自機の真上からカメラの側へ 1.5 m 張り出す
    const float topRoot = 4.5f;
    NS::OBB ceiling;
    ceiling.center = NS::Vector3{0.0f, topRoot + headAboveRoot + 0.2f + 0.5f, 0.0f} - back * 1.5f;
    ceiling.halfExtentX = 3.0f;
    ceiling.halfExtentY = 0.5f;
    ceiling.halfExtentZ = 3.0f;
    (void)dash.scene.Physics().AddBox(ceiling, NS::Phys::ObjectLayers::Terrain);

    dash.target->rebound.rebounding = true;
    dash.target->rebound.holdLook = false;
    dash.target->rebound.pullBack = false;
    dash.target->rebound.verticalLean = 1.0f;
    float shortest = distance;
    for (int frame = 1; frame <= 30; ++frame)
    {
        dash.target->Root().SetPosition(NS::Vector3{0.0f, topRoot * static_cast<float>(frame) / 30.0f, 0.0f});
        dash.Step(1);
        shortest = std::min(shortest, DistanceOf(dash.Pose()));
    }
    EXPECT_GT(shortest, distance * 0.8f);
}

// 縦の向きが壊れた反動は受けない
TEST(FollowDashRebound, RejectsABrokenVerticalLean)
{
    NS::ThirdPersonFollow follow;
    NS::FollowReboundDesc desc{};
    desc.verticalLean = std::nanf("");
    EXPECT_FALSE(follow.SetFollowRebound(desc));
    desc.verticalLean = 1.5f;
    EXPECT_FALSE(follow.SetFollowRebound(desc));
    desc.verticalLean = -1.0f;
    EXPECT_TRUE(follow.SetFollowRebound(desc));
}
