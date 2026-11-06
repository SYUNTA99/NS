#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/ThirdPersonFollow.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "Tests/TestSceneSetup.h"

#include <gtest/gtest.h>

#include <cmath>

// 速く動く間のカメラ: 縦の向きで上下の帯をずらす・視野角を広げる・自機との間の地形で縮める

namespace
{
    // 試しが書いた反動の状態を仮想カメラへ渡す、追われる物
    class DashTargetProbeActor final : public NS::SceneActor
    {
    public:
        void Feed(GL::Level::ThirdPersonFollow* vcam) const
        {
            NS_ASSERT(Game, vcam != nullptr, "Feed: 渡したアクターか部品が無い");
            vcam->SetFollowMotion(grounded, velocity);
            (void)vcam->SetFollowRebound(rebound);
        }

        GL::Level::FollowReboundDesc rebound{};
        bool grounded = false;
        NS::Vector3 velocity{};
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
                scene.GetCameraManager()->OnTick();
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

    // 床の点が画面の中央から下へどれだけ離れて映るか。中央が 0、下端が 1
    float BelowCenterOnScreen(const NS::CameraPose& pose, const NS::Vector3& point)
    {
        NS::Vector3 forward = pose.target - pose.position;
        forward.Normalize();
        NS::Vector3 right = NS::Cross(NS::Vector3{0.0f, 1.0f, 0.0f}, forward);
        right.Normalize();
        const NS::Vector3 up = NS::Cross(forward, right);
        const NS::Vector3 toPoint = point - pose.position;
        return -NS::Dot(toPoint, up) / NS::Dot(toPoint, forward) / std::tan(pose.fovY.value * 0.5f);
    }

    // 落下で足元を映す試し: 床の 8 m 上で反動を始める
    struct FallScene : DashScene
    {
        FallScene()
        {
            floor = AddTerrainBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, 100.0f, 0.5f, 100.0f);
            Step(30);
            target->Root().SetPosition(NS::Vector3{0.0f, 8.0f, 0.0f});
            target->rebound.rebounding = true;
        }

        // 本人の入力の向きから下げた角度 (度)。回り込みと向きの下げの合計
        [[nodiscard]] float AddedDegrees() const
        {
            const NS::CameraPose pose = Pose();
            NS::Vector3 forward = pose.target - pose.position;
            forward.Normalize();
            return NS::RadiansToDegrees(camera->Vcam().Pitch() - std::asin(forward.y));
        }

        [[nodiscard]] float FloorBelowCenter() const
        {
            const NS::Vector3 root = target->Root().Position();
            return BelowCenterOnScreen(Pose(), NS::Vector3{root.x, 0.0f, root.z});
        }

        [[nodiscard]] float StartSpeed() const
        {
            return NS::SerializeClassObjFields(&camera->Vcam()).at("足元を映し始める縦の速さ").get<float>();
        }

        JPH::BodyID floor{};
    };
} // namespace

// 落ち始めると、真下の床が画面の下端の内側へ入る
TEST(FollowDashRebound, FallingBringsTheFloorInsideTheBottomEdge)
{
    FallScene fall;
    fall.target->velocity = NS::Vector3{0.0f, fall.StartSpeed() + 1.0f, 0.0f};
    fall.Step(30);
    ASSERT_GT(fall.FloorBelowCenter(), 1.0f);

    fall.target->velocity = NS::Vector3{0.0f, -1.0f, 0.0f};
    fall.Step(60);
    EXPECT_LT(fall.FloorBelowCenter(), 1.0f);
}

// 縦の速さが足元を映し始める速さにある間は角度を足さず、下回ると上昇中でも足し始める
TEST(FollowDashRebound, StartsAddingAngleBelowTheStartSpeed)
{
    FallScene fall;
    fall.target->velocity = NS::Vector3{0.0f, fall.StartSpeed(), 0.0f};
    fall.Step(60);
    EXPECT_NEAR(fall.AddedDegrees(), 0.0f, 1.0e-3f);

    fall.target->velocity = NS::Vector3{0.0f, fall.StartSpeed() - 0.1f, 0.0f};
    ASSERT_GT(fall.target->velocity.y, 0.0f);
    fall.Step(60);
    EXPECT_GT(fall.AddedDegrees(), 1.0f);
}

// 接地すると足した角度を戻す
TEST(FollowDashRebound, LandingReturnsTheAddedAngle)
{
    FallScene fall;
    fall.target->velocity = NS::Vector3{0.0f, -1.0f, 0.0f};
    fall.Step(60);
    ASSERT_GT(fall.AddedDegrees(), 1.0f);

    fall.target->Root().SetPosition(NS::Vector3{0.0f, 1.0f, 0.0f});
    fall.target->rebound.rebounding = false;
    fall.target->grounded = true;
    fall.target->velocity = NS::Vector3{};
    fall.Step(90);
    EXPECT_LT(fall.AddedDegrees(), 0.1f);
}

// 床が近づいても、接地するまでは足した角度を戻さない。落下の途中で回転の向きが逆にならない
TEST(FollowDashRebound, DoesNotReturnTheAddedAngleWhileFalling)
{
    FallScene fall;
    fall.target->velocity = NS::Vector3{0.0f, -1.0f, 0.0f};
    fall.Step(60);
    const float before = fall.AddedDegrees();
    ASSERT_GT(before, 1.0f);

    for (int frame = 0; frame < 60; ++frame)
    {
        fall.target->Root().SetPosition(NS::Vector3{0.0f, 8.0f - 0.1f * static_cast<float>(frame), 0.0f});
        fall.Step(1);
    }
    EXPECT_GE(fall.AddedDegrees(), before - 0.01f);
}

// 真下に床が無くなっても、前のフレームの目標を保つ
TEST(FollowDashRebound, KeepsTheGoalWhenNoFloorIsBelow)
{
    FallScene fall;
    fall.target->velocity = NS::Vector3{0.0f, -1.0f, 0.0f};
    fall.Step(180); // バネが目標へ寄りきってから比べる
    const float before = fall.AddedDegrees();
    ASSERT_GT(before, 1.0f);

    fall.scene.Physics().RemoveBody(fall.floor);
    fall.Step(60);
    EXPECT_NEAR(fall.AddedDegrees(), before, 0.5f);
}

// ピッチの上限で上へ入力すると、ピッチが変わらなくても足した角度を戻す
TEST(FollowDashRebound, PitchInputAtTheLimitReturnsTheAddedAngle)
{
    FallScene fall;
    fall.target->velocity = NS::Vector3{0.0f, -1.0f, 0.0f};
    fall.Step(60);
    ASSERT_GT(fall.AddedDegrees(), 1.0f);

    const float pitch = fall.camera->Vcam().Pitch();
    NS::ApplyJsonFields(&fall.camera->Vcam(), nlohmann::json{{"ピッチ上限", pitch}, {"反転 Y", false}});

    NS::OS::Mouse& mouse = NS::OS::Input::Get().Mouse();
    mouse.SetRelativeMode(true);
    mouse.OnRawMove(0, 10);
    fall.Step(1);
    mouse.SetRelativeMode(false);
    ASSERT_EQ(fall.camera->Vcam().Pitch(), pitch);

    fall.Step(90);
    EXPECT_LT(fall.AddedDegrees(), 0.1f);
}

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
    const JPH::BodyID body = AddTerrainBox(dash.scene, open.target + backward * 2.5f, 10.0f, 10.0f, 0.5f);
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
    (void)AddTerrainBox(dash.scene, NS::Vector3{0.0f, 0.0f, 1.6f}, 10.0f, 20.0f, 1.0f);

    dash.target->rebound.rebounding = true;
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
    (void)AddTerrainBox(dash.scene, NS::Vector3{0.0f, headY + 0.1f, 0.0f} - back * 0.6f, 3.0f, 0.5f, 0.9f);

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
    (void)AddTerrainBox(dash.scene,
                        NS::Vector3{0.0f, topRoot + headAboveRoot + 0.2f + 0.5f, 0.0f} - back * 1.5f,
                        3.0f,
                        0.5f,
                        3.0f);

    dash.target->rebound.rebounding = true;
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

// 天井の端を出た頭のすぐ横に天井の側面があっても、自機の体からカメラが見えていれば縮めない
TEST(FollowDashRebound, TerrainBesideTheHeadDoesNotCollapseWhenTheBodySeesTheCamera)
{
    DashScene dash;
    dash.Step(120);
    const NS::CameraPose open = dash.Pose();
    const float distance = DistanceOf(open);
    NS::Vector3 back = open.position - open.target;
    back.y = 0.0f;
    back.Normalize();
    ASSERT_GT(std::fabs(back.z), 0.99f);
    ASSERT_GT(open.position.y, open.target.y);

    // 頭の高さを中に含み、頭のカメラ側 0.05 m から 0.5 m の幅で左右へ伸びる細い地形。頭からカメラへの線だけを遮る
    NS::OBB beam;
    beam.center = open.target + back * 0.3f;
    beam.halfExtentX = 3.0f;
    beam.halfExtentY = 0.3f;
    beam.halfExtentZ = 0.25f;
    (void)dash.scene.Physics().AddBox(beam, NS::Phys::k_TerrainObjectLayer);

    float shortest = distance;
    for (int frame = 0; frame < 10; ++frame)
    {
        dash.Step(1);
        shortest = std::min(shortest, DistanceOf(dash.Pose()));
    }
    EXPECT_GT(shortest, distance * 0.8f);
}

// 縦の向きが壊れた反動は受けない
TEST(FollowDashRebound, RejectsABrokenVerticalLean)
{
    GL::Level::ThirdPersonFollow follow;
    GL::Level::FollowReboundDesc desc{};
    desc.verticalLean = std::nanf("");
    EXPECT_FALSE(follow.SetFollowRebound(desc));
    desc.verticalLean = 1.5f;
    EXPECT_FALSE(follow.SetFollowRebound(desc));
    desc.verticalLean = -1.0f;
    EXPECT_TRUE(follow.SetFollowRebound(desc));
}
