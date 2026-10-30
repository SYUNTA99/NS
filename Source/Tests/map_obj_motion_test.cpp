#include "Editor/EditorActors.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Windows/Clock.h"

#include <gtest/gtest.h>

namespace
{
    GL::Level::PropActor* PlaceMovingRock(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json rock = NS::MakeActorJson();
        NS::SetActorJsonFile(rock, "PropActor");
        NS::SetActorJsonId(rock, 1);
        NS::Editor::SetActorJsonPosition(rock, NS::Vector3{0.0f, 0.5f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        return static_cast<GL::Level::PropActor*>(scene.Actors().FindByActorId(1));
    }

    void ReleaseRock(GL::Level::PropActor* rock)
    {
        NS_ASSERT(Game, rock != nullptr, "ReleaseRock: 渡したアクターか部品が無い");
        GL::Level::TackleReleaseDesc release;
        release.arc.distance = 5.0f;
        release.arc.apexHeight = 1.0f;
        EXPECT_TRUE(GL::Level::SendMsgTackleRelease(rock, release));
    }
} // namespace

TEST(MapObjMotion, ActorMovesAndUpdatesStaticCollision)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_NE(rock->GetStateMachine(), nullptr);
    ReleaseRock(rock);
    for (int step = 0; step < 10; ++step)
    {
        rock->UpdateMotion();
    }
    EXPECT_GT(rock->Root().Position().x, 1.0f);
    EXPECT_GT(rock->Root().Position().y, 0.5f);
    float distance = 0.0f;
    const NS::Vector3 from = rock->Root().Position() + NS::Vector3{0.0f, 2.0f, 0.0f};
    ASSERT_TRUE(scene.Physics().Raycast(from, NS::Vector3{0.0f, -1.0f, 0.0f}, 3.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 0.001f);
}

TEST(MapObjMotion, LandingRollsThenReturnsToRest)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::OBB floor;
    floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentY = 0.5f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(rock);
    for (int step = 0; step < 300; ++step)
    {
        rock->UpdateMotion();
    }
    GL::Level::TackleTargetAnswer answer;
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(rock->HitSensorsClassObj()->Find("Body"), answer));
    EXPECT_TRUE(answer.placed);
    EXPECT_GT(rock->Root().Position().x, 5.0f);
    EXPECT_NEAR(rock->Root().Position().y, 0.5f, 0.002f);
    const NS::Vector3 stopped = rock->Root().Position();
    rock->UpdateMotion();
    EXPECT_FLOAT_EQ(rock->Root().Position().x, stopped.x);
    EXPECT_FLOAT_EQ(rock->Root().Position().y, stopped.y);
}

TEST(MapObjMotion, WallReflectsFlightBeforeItCanTunnel)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::OBB wall;
    wall.center = NS::Vector3{2.0f, 2.0f, 0.0f};
    wall.halfExtentX = 0.1f;
    wall.halfExtentY = 4.0f;
    wall.halfExtentZ = 4.0f;
    scene.Physics().AddBox(wall, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(rock);
    float previous = rock->Root().Position().x;
    bool reflected = false;
    for (int step = 0; step < 35; ++step)
    {
        rock->UpdateMotion();
        const float current = rock->Root().Position().x;
        EXPECT_LE(current, 1.401f);
        if (current < previous)
        {
            reflected = true;
        }
        previous = current;
    }
    EXPECT_TRUE(reflected);
}

TEST(MapObjMotion, UnobstructedFlightKeepsAnalyticArc)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    ReleaseRock(rock);
    GL::Level::LaunchArc arc;
    arc.distance = 5.0f;
    arc.apexHeight = 1.0f;
    float seconds = 0.0f;
    for (int step = 0; step < 45; ++step)
    {
        seconds += NS::OS::FrameTimer::FixedDelta();
        rock->UpdateMotion();
        const NS::Vector3 expected = GL::Level::LaunchArcOffsetAt(arc, seconds);
        EXPECT_NEAR(rock->Root().Position().x, expected.x, 0.0001f);
        EXPECT_NEAR(rock->Root().Position().y, 0.5f + expected.y, 0.0001f);
    }
}

TEST(MapObjMotion, RestartCancelsFlightAndRestoresPlacement)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(GL::Level::SendMsgCourseStart(rock));
    ReleaseRock(rock);
    for (int step = 0; step < 10; ++step)
    {
        rock->UpdateMotion();
    }
    ASSERT_TRUE(GL::Level::SendMsgCourseRestart(rock));
    EXPECT_FALSE(rock->IsFlying());
    rock->UpdateMotion();
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 0.0f);
    EXPECT_FLOAT_EQ(rock->Root().Position().y, 0.5f);
    EXPECT_FLOAT_EQ(rock->Velocity().Length(), 0.0f);
}

// やり直しの知らせはコースの段で届き、次のフレームはアクターの段で並び順に回るので、自機が置物より先に回る事がある。
// 置き直した当たりは、置物の次の更新を待たずに知らせを受けたその場で戻った位置にある
TEST(MapObjMotion, RestartMovesTheCollisionBeforeTheNextUpdate)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(GL::Level::SendMsgCourseStart(rock));
    ReleaseRock(rock);
    for (int step = 0; step < 10; ++step)
    {
        rock->UpdateMotion();
    }
    const NS::Vector3 aboveHome{0.0f, 2.5f, 0.0f};
    float distance = 0.0f;
    ASSERT_FALSE(scene.Physics().Raycast(aboveHome, NS::Vector3{0.0f, -1.0f, 0.0f}, 3.0f, distance));
    ASSERT_TRUE(GL::Level::SendMsgCourseRestart(rock));
    ASSERT_TRUE(scene.Physics().Raycast(aboveHome, NS::Vector3{0.0f, -1.0f, 0.0f}, 3.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 0.001f);
}

TEST(MapObjMotion, LandingNotificationEmitsDustWithoutRigidContacts)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::OBB floor;
    floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(rock);
    for (int step = 0; step < 55; ++step)
    {
        rock->UpdateMotion();
    }
    const NS::EffectObj* effect = rock->GetEffect();
    ASSERT_NE(effect, nullptr);
    int dustCount = 0;
    for (const NS::EffectRecord& record : effect->Records())
    {
        if (record.name == "launch.landDust")
        {
            ++dustCount;
            ASSERT_TRUE(record.amount.has_value());
            EXPECT_FLOAT_EQ(*record.amount, 0.0f);
        }
    }
    EXPECT_EQ(dustCount, 1);
}

TEST(MapObjMotion, EffectClockIsSeparateFromItsPostMovementUpdate)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    const NS::EffectObj* effect = rock->GetEffect();
    ASSERT_NE(effect, nullptr);
    GL::Level::LaunchEffects* effects = &rock->Effects();
    rock->Update();
    EXPECT_EQ(effect->Step(), 1);
    effects->Update(rock, rock->Flight());
    EXPECT_EQ(effect->Step(), 1);
}

TEST(MapObjMotion, TrailAndLandingRecordsKeepTheReleaseStrength)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    GL::Level::TackleReleaseDesc release;
    release.arc.distance = 5.0f;
    release.arc.apexHeight = 1.0f;
    release.power = 2.0f;
    release.launchScale = 1.5f;
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(rock, release));
    rock->Effects().NotifyLanding(rock, NS::Vector3::Zero, NS::Vector3::UnitY);
    bool trail = false;
    bool dust = false;
    for (const NS::EffectRecord& record : rock->GetEffect()->Records())
    {
        if (record.name == "launch.trail")
        {
            trail = true;
            ASSERT_TRUE(record.amount.has_value());
            EXPECT_FLOAT_EQ(*record.amount, release.launchScale);
        }
        if (record.name == "launch.landDust")
        {
            dust = true;
            ASSERT_TRUE(record.amount.has_value());
            EXPECT_FLOAT_EQ(*record.amount, release.power);
        }
    }
    EXPECT_TRUE(trail);
    EXPECT_TRUE(dust);
}

TEST(MapObjMotion, LandingDustStartsOnTheActorUpdateThatLands)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::OBB floor;
    floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(rock);
    const NS::EffectObj* effect = rock->GetEffect();
    ASSERT_NE(effect, nullptr);
    bool landed = false;
    for (int step = 0; step < 100 && !landed; ++step)
    {
        rock->Update();
        for (const NS::EffectRecord& record : effect->Records())
        {
            if (record.name == "launch.landDust")
            {
                EXPECT_EQ(record.startStep, effect->Step());
                EXPECT_FALSE(rock->IsArc());
                landed = true;
            }
        }
    }
    EXPECT_TRUE(landed);
}

TEST(MapObjMotion, StateAdvancesThroughTheBaseMachine)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::IStateMachine* machine = rock->GetStateMachine();
    ASSERT_NE(machine, nullptr);
    // 先頭の状態へは最初の Update の初めに入り、その Update で 1 歩目を進める
    rock->Update();
    ASSERT_EQ(machine->StepCount(), 1u);
    rock->Update();
    EXPECT_EQ(machine->StepCount(), 2u);
    EXPECT_EQ(rock->GetStateMachine(), machine);
}
