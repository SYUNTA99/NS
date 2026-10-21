#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/TackleGhost.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestLogCapture.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string_view>

// 体当たりを受けた置物が、当たったと明けの知らせを受けて、自分の .actor の束で止まり・揺れ・飛ぶ事を縛る

namespace
{
    NS::SceneActor* PlaceRock(NS::Scene& scene, const NS::Vector3& position)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(rock, "PropActor");
        NS::SetObjectJsonId(rock, 1);
        NS::SetObjectPosition(rock, position);
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        return scene.Objects().FindByObjectId(1);
    }

    // 自機を原点の上に、置物を z 4 に置き、後ろ上の固定のカメラから置物を見る
    GL::Level::PropActor* PlaceRockBeforePlayer(NS::Scene& scene, std::string_view actorFile = "PropActor")
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(player, "PlayerActor");
        NS::SetObjectJsonId(player, 1);
        NS::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(rock, actorFile);
        NS::SetObjectJsonId(rock, 2);
        NS::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 4.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        PlaceViewCamera(scene, NS::Vector3{0.0f, 3.0f, -6.0f}, NS::Vector3{0.0f, 0.5f, 4.0f});
        return NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
    }

    PlayerActor* PlayerOf(NS::Scene& scene)
    {
        return NS::Cast<PlayerActor>(scene.Objects().FindByObjectId(1));
    }

    // 自機の体のセンサーから、置物の手前の面を +z へ押した当たり
    bool SendHit(NS::Scene& scene,
                 GL::Level::PropActor& rock,
                 GL::Level::HitTier tier,
                 std::optional<int> stopOverride = std::nullopt)
    {
        PlayerActor* player = PlayerOf(scene);
        if (player == nullptr)
        {
            return false;
        }
        const GL::Level::TackleHitDesc hit{.tier = tier,
                                           .contact = NS::Vector3{0.0f, 0.5f, 3.5f},
                                           .impactDir = NS::Vector3{0.0f, 0.0f, 1.0f},
                                           .stopOverride = stopOverride};
        return GL::Level::SendMsgTackleHit(*rock.BodySensorClassObj(), *player->BodySensorClassObj(), hit);
    }

    GL::Level::TackleReleaseDesc ForwardRelease()
    {
        GL::Level::TackleReleaseDesc release{};
        release.arc =
            GL::Level::LaunchArc{.direction = NS::Vector3{0.0f, 0.0f, 1.0f}, .distance = 5.0f, .apexHeight = 1.0f};
        return release;
    }

    // 真上から下へ線を引き、置物の球に当たるか。置物の当たりを、次の更新を待たずに揃えたかを見る
    bool RayHitsTheRockAt(NS::Scene& scene, float z)
    {
        float distance = 0.0f;
        return scene.Physics().Raycast(NS::Vector3{0.0f, 2.5f, z}, NS::Vector3{0.0f, -1.0f, 0.0f}, 3.0f, distance);
    }
} // namespace

TEST(MapObjTackle, RockAnswersTheAsk)
{
    NS::Scene scene;
    NS::SceneActor* rock = PlaceRock(scene, NS::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    NS::HitSensor* body = rock->BodySensorClassObj();
    ASSERT_NE(body, nullptr);

    GL::Level::TackleTargetAnswer answer{};
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(*body, answer));
    EXPECT_TRUE(answer.placed);
    // 壊れる動きはまだ無いので、破壊を許しても自機を貫通させない
    EXPECT_FALSE(answer.breakable);
    EXPECT_GT(answer.mass, 0.0f);
    EXPECT_FLOAT_EQ(answer.position.x, 2.0f);
    EXPECT_FLOAT_EQ(answer.bounds.Center.x, 2.0f);
}

// 当たったの知らせで受けた状態に入り、自分の束 受け[真ん中] の止めの長さ 12 だけ止まる。
// 食い込み 0.06 m と潰れ (厚み 0.7・伸び 1.1) を出し、止めが明けた最初の状態の歩で元の位置と形へ戻る
TEST(MapObjTackle, AHitStopsTheRockForItsOwnSet)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_FALSE(RayHitsTheRockAt(scene, 4.53f));

    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    EXPECT_TRUE(rock->IsFrozen());
    EXPECT_EQ(rock->HitStopFramesRemaining(), 12);
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.06f);
    // 置き直した当たりは、次の更新を待たずに食い込んだ所にある
    EXPECT_TRUE(RayHitsTheRockAt(scene, 4.53f));
    const NS::Vector3 squashed = rock->ModelClassObj()->GetDrawScale();
    EXPECT_FLOAT_EQ(squashed.x, 1.0f);
    EXPECT_FLOAT_EQ(squashed.y, 1.1f);
    EXPECT_FLOAT_EQ(squashed.z, 0.7f);

    for (int frame = 1; frame <= 12; ++frame)
    {
        SCOPED_TRACE(frame);
        rock->UpdateMotion();
        EXPECT_TRUE(rock->IsFrozen());
        EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.06f);
    }
    rock->UpdateMotion();
    EXPECT_FALSE(rock->IsFrozen());
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.0f);
    EXPECT_TRUE(rock->ModelClassObj()->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
}

// 外れは自分の束 受け[外れ] の止めの長さ 4 だけ止まる
TEST(MapObjTackle, AMissStopsTheRockForItsMissSet)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Wide));
    EXPECT_TRUE(rock->IsFrozen());
    EXPECT_EQ(rock->HitStopFramesRemaining(), 4);
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.06f);
}

// 止めの間の横揺れと残像は、置物の束の値 (横揺れ 45 ピクセル・残像の離れ 112.5 ピクセル) で、束の長さ 12 だけ出す。
// 残像の離れは食い込んだ所の奥行きでピクセルを m にし、止めの残りの割合を掛ける
TEST(MapObjTackle, TheRockShakesAndSpreadsGhostsFromItsOwnSet)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(rock);
    ASSERT_TRUE(pose.has_value());
    const NS::Vector3 forward = NS::CalcCameraForwardHorizontal(rock);
    const float spread = NS::ScreenPixelsToMeters(112.5f, *pose, rock->Root().Position());
    const float shake = NS::ScreenPixelsToMeters(45.0f, *pose, rock->Root().Position());
    for (int frame = 1; frame <= 14; ++frame)
    {
        SCOPED_TRACE(frame);
        rock->UpdateMotion();
        const GL::Level::TackleGhost& ghost = rock->Ghost();
        const NS::Vector3 offset = rock->ModelClassObj()->GetDrawOffset();
        if (frame < 12)
        {
            EXPECT_NEAR(ghost.Spread().Length(), NS::BodyShakeReach(frame, 12, spread), 1.0e-5f);
            EXPECT_NEAR(ghost.Spread().Dot(forward), 0.0f, 1.0e-5f);
            EXPECT_FLOAT_EQ(ghost.SideOpacity(), 0.45f);
            EXPECT_GT(offset.Length(), 0.0f);
            EXPECT_LE(offset.Length(), shake + 1.0e-5f);
            EXPECT_NEAR(offset.Dot(forward), 0.0f, 1.0e-5f);
            continue;
        }
        EXPECT_FLOAT_EQ(ghost.Spread().Length(), 0.0f);
        EXPECT_FLOAT_EQ(ghost.AfterimageOpacity(), 0.0f);
        EXPECT_FLOAT_EQ(offset.Length(), 0.0f);
    }
}

// 明けの知らせで、食い込んだ所で自分の明けの束 (震え 10 ピクセル・長さ 12) を始めてから、元の位置へ戻って飛ぶ
TEST(MapObjTackle, TheReleaseTremblesFromItsOwnSetThenLaunches)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    for (int frame = 0; frame < 12; ++frame)
    {
        rock->UpdateMotion();
    }
    const NS::Vector3 pushedIn = rock->Root().Position();
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, ForwardRelease()));
    EXPECT_FALSE(rock->IsFrozen());
    EXPECT_TRUE(rock->IsArc());
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.0f);
    // 明けで戻した当たりも、次の更新を待たずに元の位置にある
    EXPECT_FALSE(RayHitsTheRockAt(scene, 4.53f));
    rock->UpdateMotion();
    const NS::Gfx::TremorCB tremor = rock->ModelClassObj()->GetDrawTremor();
    EXPECT_GT(tremor.amplitude, 0.0f);
    // 触れた点は、震えを始めた時の食い込んだ根からのずれで持つ
    EXPECT_NEAR(tremor.contactOffset.z, 3.5f - pushedIn.z, 1.0e-5f);
}

// 当たっていない置物へ明けだけを送ると、震えずに飛ぶ
TEST(MapObjTackle, AReleaseWithoutAHitDoesNotTremble)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, ForwardRelease()));
    rock->UpdateMotion();
    EXPECT_TRUE(rock->IsArc());
    EXPECT_FLOAT_EQ(rock->ModelClassObj()->GetDrawTremor().amplitude, 0.0f);
}

namespace
{
    float RockRadius(const GL::Level::PropActor& rock)
    {
        return static_cast<const NS::SphereCollision*>(rock.CollisionClassObj())->WorldSphere().radius;
    }

    // 当てて止めを明けさせ、明けの知らせを送る。返すのは震えを始めた時の根
    NS::Vector3 HitAndRelease(NS::Scene& scene, GL::Level::PropActor& rock)
    {
        EXPECT_TRUE(SendHit(scene, rock, GL::Level::HitTier::Center));
        for (int frame = 0; frame < 12; ++frame)
        {
            rock.UpdateMotion();
        }
        const NS::Vector3 pushedIn = rock.Root().Position();
        EXPECT_TRUE(GL::Level::SendMsgTackleRelease(rock, ForwardRelease()));
        return pushedIn;
    }

    // 出荷の PropActor.actor の明け[真ん中] の震えの欄だけを fields で書き換えた .actor
    nlohmann::json TremorRockFile(const nlohmann::json& fields)
    {
        const nlohmann::json* shipped = NS::ActorFileLibrary::Get().Find("PropActor");
        EXPECT_NE(shipped, nullptr);
        if (shipped == nullptr)
        {
            return nlohmann::json::object();
        }
        nlohmann::json file = *shipped;
        for (nlohmann::json& set : file["hitReactions"])
        {
            if (set["name"] == "明け[真ん中]")
            {
                set["items"][0]["fields"].update(fields);
            }
        }
        return file;
    }
} // namespace

// 明けの束の震えは、明けのフレームを 0 にした経過で束の長さ 12 だけ続き、裏へ届くのは欄 6 フレームで体の差し渡しまで。
// 振れ幅は 10 ピクセルを毎フレーム置物とカメラの距離で m にし、触れた点のずれは始めた時のまま動かない
TEST(MapObjTackle, TheReleaseTremorRunsForItsSetLength)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    (void)HitAndRelease(scene, *rock);
    NS::Vector3 contact{};
    for (int frame = 1; frame <= 14; ++frame)
    {
        SCOPED_TRACE(frame);
        rock->UpdateMotion();
        const NS::Gfx::TremorCB tremor = rock->ModelClassObj()->GetDrawTremor();
        if (frame > 12)
        {
            EXPECT_FLOAT_EQ(tremor.amplitude, 0.0f);
            continue;
        }
        const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(rock);
        ASSERT_TRUE(pose.has_value());
        EXPECT_FLOAT_EQ(tremor.elapsedFrames, static_cast<float>(frame - 1));
        EXPECT_FLOAT_EQ(tremor.ringFrames, 6.0f);
        EXPECT_NEAR(tremor.framesPerMeter, 6.0f / (2.0f * RockRadius(*rock)), 1.0e-4f);
        EXPECT_NEAR(tremor.amplitude, NS::ScreenPixelsToMeters(10.0f, *pose, rock->Root().Position()), 1.0e-5f);
        if (frame == 1)
        {
            contact = tremor.contactOffset;
        }
        EXPECT_TRUE(tremor.contactOffset == contact);
    }
}

// 明けの束の震えの欄が置物の描く震えに届く。体の半径に対する割合が正なら、飛んで遠ざかっても 割合 × 半径 のまま。
// 割合が 0 ならピクセルの欄を、ピクセルを数える画面の高さで数える
TEST(MapObjTackle, TheReleaseTremorReadsItsSetFields)
{
    {
        const ScopedActorFile file("RatioTremorRock", TremorRockFile({{"体の半径に対する振れ幅の割合", 0.14}}));
        NS::Scene scene;
        GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene, "RatioTremorRock");
        ASSERT_NE(rock, nullptr);
        (void)HitAndRelease(scene, *rock);
        const float startZ = rock->Root().Position().z;
        for (int frame = 1; frame <= 12; ++frame)
        {
            SCOPED_TRACE(frame);
            rock->UpdateMotion();
            EXPECT_NEAR(rock->ModelClassObj()->GetDrawTremor().amplitude, 0.14f * RockRadius(*rock), 1.0e-6f);
        }
        // 震えの間に遠ざかっている。ピクセルの欄なら振れ幅が伸びる場面
        EXPECT_GT(rock->Root().Position().z, startZ + 0.5f);
    }
    {
        const ScopedActorFile file("ReferenceTremorRock", TremorRockFile({{"ピクセルを数える画面の高さ", 1440.0}}));
        NS::Scene scene;
        GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene, "ReferenceTremorRock");
        ASSERT_NE(rock, nullptr);
        (void)HitAndRelease(scene, *rock);
        rock->UpdateMotion();
        const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(rock);
        ASSERT_TRUE(pose.has_value());
        EXPECT_NEAR(rock->ModelClassObj()->GetDrawTremor().amplitude,
                    NS::ScreenPixelsToMeters(10.0f, *pose, rock->Root().Position()) * 0.5f,
                    1.0e-5f);
    }
}

// 貫通の止めの上書きは、置物の止めを束の長さから書き換え、押し勝っている側なので潰さない
TEST(MapObjTackle, AStopOverrideReplacesTheSetsStopAndDoesNotSquash)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center, 20));
    EXPECT_TRUE(rock->IsFrozen());
    EXPECT_EQ(rock->HitStopFramesRemaining(), 20);
    EXPECT_TRUE(rock->ModelClassObj()->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    for (int frame = 0; frame < 20; ++frame)
    {
        rock->UpdateMotion();
    }
    EXPECT_TRUE(rock->IsFrozen());
    rock->UpdateMotion();
    EXPECT_FALSE(rock->IsFrozen());
}

TEST(MapObjTackle, FlyingRockIsNotPushedIn)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, ForwardRelease()));

    GL::Level::TackleTargetAnswer answer{};
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(*rock->BodySensorClassObj(), answer));
    EXPECT_FALSE(answer.placed);

    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.0f);
    EXPECT_TRUE(rock->IsFlying());
}

TEST(MapObjTackle, DestructionRemainsDisabled)
{
    NS::Scene scene;
    NS::SceneActor* rock = PlaceRock(scene, NS::Vector3{2.0f, 1.0f, 0.0f});
    ASSERT_NE(rock, nullptr);
    GL::Level::TackleReleaseDesc release{};
    release.breaks = true;
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, release));
    EXPECT_TRUE(rock->BodySensorClassObj()->IsValid());
    EXPECT_TRUE(rock->IsAppeared());
}

TEST(MapObjTackle, ANewHitDoesNotAccumulatePushIn)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.06f);
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.06f);
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, ForwardRelease()));
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.0f);
    EXPECT_TRUE(rock->IsArc());
}

// 止めの途中のやり直しは、受けた状態と止めを解いて置いた所へ戻し、明けを送られても震えない
TEST(MapObjTackle, ARestartClearsTheRocksStopAndHit)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(GL::Level::SendMsgCourseStart(*rock));
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    rock->UpdateMotion();
    ASSERT_TRUE(GL::Level::SendMsgCourseRestart(*rock));
    EXPECT_FALSE(rock->IsFrozen());
    EXPECT_EQ(rock->HitStopFramesRemaining(), 0);
    EXPECT_FLOAT_EQ(rock->Root().Position().z, 4.0f);
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, ForwardRelease()));
    rock->UpdateMotion();
    EXPECT_FLOAT_EQ(rock->ModelClassObj()->GetDrawTremor().amplitude, 0.0f);
}

// 置物が消えても、自機が頼んだカメラの効果は残る。置物が外すのは自分の頼んだ効果だけ
TEST(MapObjTackle, ARockDisappearingKeepsThePlayersCameraEffects)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockBeforePlayer(scene);
    ASSERT_NE(rock, nullptr);
    PlayerActor* player = PlayerOf(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_NE(rock->HitReactionClassObj(), nullptr);
    ASSERT_TRUE(player->HitReactionClassObj()->StartSink(
        NS::CameraSinkDesc{.bottomPixels = 9.0f, .bounceStartFrame = 30, .frames = 30}));
    ASSERT_TRUE(SendHit(scene, *rock, GL::Level::HitTier::Center));
    rock->Disappear();
    EXPECT_NE(player->GetCameraManager()->FindModifier<NS::CameraSinkModifier>(), nullptr);
}

// 束の値をフレーム数に読む時、非数・無限・負・int に入らない値は WARN を出して既定の数にする。
// 小数は 0 の側へ切り捨てる
TEST(HitOwnerFrames, ValuesOutsideTheFrameRangeFallBack)
{
    NS::HitOwnerItem item;
    item.name = "TackleGhost";
    item.values = {{"half", 2.7f},
                   {"nan", std::numeric_limits<float>::quiet_NaN()},
                   {"inf", std::numeric_limits<float>::infinity()},
                   {"negative", -1.0f},
                   {"over", 2147483648.0f}};
    const TestLogCapture log;
    EXPECT_EQ(GL::Level::HitOwnerFrames(item, "half", 5), 2);
    EXPECT_EQ(log.WarningCount(), 0);
    for (const char* name : {"nan", "inf", "negative", "over"})
    {
        SCOPED_TRACE(name);
        EXPECT_EQ(GL::Level::HitOwnerFrames(item, name, 5), 5);
    }
    EXPECT_EQ(log.WarningCount(), 4);
}
