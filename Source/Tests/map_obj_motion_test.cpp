#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObj.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Clock.h"

#include <gtest/gtest.h>

namespace
{
    NS::Game::Level::MapObj* PlaceMovingRock(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 1);
        NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{0.0f, 0.5f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        return static_cast<NS::Game::Level::MapObj*>(scene.Objects().FindByObjectId(1));
    }

    void ReleaseRock(NS::Game::Level::MapObj& rock)
    {
        NS::Game::Level::TackleReleaseDesc release;
        release.arc.distance = 5.0f;
        release.arc.apexHeight = 1.0f;
        EXPECT_TRUE(NS::Game::Level::SendMsgTackleRelease(rock, release));
    }
} // namespace

TEST(MapObjMotion, ActorMovesWithoutRigidBodyAndUpdatesStaticCollider)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::RigidBody>(rock->Part("RigidBody")), nullptr);
    ASSERT_NE(rock->GetStateMachine(), nullptr);
    ReleaseRock(*rock);
    for (int step = 0; step < 10; ++step)
    {
        rock->UpdateMotion();
    }
    EXPECT_GT(rock->Root().Position().x, 1.0f);
    EXPECT_GT(rock->Root().Position().y, 0.5f);
    float distance = 0.0f;
    const NS::Core::Vector3 from = rock->Root().Position() + NS::Core::Vector3{0.0f, 2.0f, 0.0f};
    ASSERT_TRUE(scene.Physics().Raycast(from, NS::Core::Vector3{0.0f, -1.0f, 0.0f}, 3.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 0.001f);
}

TEST(MapObjMotion, LandingRollsThenReturnsToRest)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Core::OBB floor;
    floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentY = 0.5f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(*rock);
    for (int step = 0; step < 300; ++step)
    {
        rock->UpdateMotion();
    }
    NS::Game::Level::TackleTargetAnswer answer;
    ASSERT_TRUE(NS::Game::Level::SendMsgAskTackleTarget(*rock->BodySensorPart(), answer));
    EXPECT_TRUE(answer.placed);
    EXPECT_GT(rock->Root().Position().x, 5.0f);
    EXPECT_NEAR(rock->Root().Position().y, 0.5f, 0.002f);
    const NS::Core::Vector3 stopped = rock->Root().Position();
    rock->UpdateMotion();
    EXPECT_FLOAT_EQ(rock->Root().Position().x, stopped.x);
    EXPECT_FLOAT_EQ(rock->Root().Position().y, stopped.y);
}

TEST(MapObjMotion, WallReflectsFlightBeforeItCanTunnel)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Core::OBB wall;
    wall.center = NS::Core::Vector3{2.0f, 2.0f, 0.0f};
    wall.halfExtentX = 0.1f;
    wall.halfExtentY = 4.0f;
    wall.halfExtentZ = 4.0f;
    scene.Physics().AddBox(wall, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(*rock);
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
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    ReleaseRock(*rock);
    NS::Game::Level::LaunchArc arc;
    arc.distance = 5.0f;
    arc.apexHeight = 1.0f;
    float seconds = 0.0f;
    for (int step = 0; step < 45; ++step)
    {
        seconds += NS::Platform::FrameTimer::FixedDelta();
        rock->UpdateMotion();
        const NS::Core::Vector3 expected = NS::Game::Level::LaunchArcOffsetAt(arc, seconds);
        EXPECT_NEAR(rock->Root().Position().x, expected.x, 0.0001f);
        EXPECT_NEAR(rock->Root().Position().y, 0.5f + expected.y, 0.0001f);
    }
}

TEST(MapObjMotion, SceneGravityRotatesTheFlightPlane)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    scene.SetGravityDirection(NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ReleaseRock(*rock);
    for (int step = 0; step < 10; ++step)
    {
        rock->UpdateMotion();
    }
    EXPECT_GT(rock->Root().Position().x, 1.0f);
    EXPECT_LT(rock->Root().Position().z, -0.5f);
    EXPECT_FLOAT_EQ(rock->Root().Position().y, 0.5f);
}

TEST(MapObjMotion, RestartCancelsFlightAndRestoresPlacement)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    const nlohmann::json baseline = scene.ToJson();
    ReleaseRock(*rock);
    for (int step = 0; step < 10; ++step)
    {
        rock->UpdateMotion();
    }
    ASSERT_TRUE(NS::Game::Level::SendMsgCourseRestart(*rock, baseline));
    EXPECT_FALSE(rock->IsFlying());
    rock->UpdateMotion();
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 0.0f);
    EXPECT_FLOAT_EQ(rock->Root().Position().y, 0.5f);
    EXPECT_FLOAT_EQ(rock->Velocity().Length(), 0.0f);
}

TEST(MapObjMotion, ZeroLengthFreezeStillWaitsForReleaseGrace)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Game::Level::TackleFreezeDesc freeze;
    freeze.pushInDistance = 0.1f;
    freeze.squashThickness = 0.8f;
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleFreeze(*rock, freeze));
    for (int step = 0; step < 3; ++step)
    {
        rock->UpdateMotion();
    }
    EXPECT_TRUE(rock->IsFrozen());
    rock->UpdateMotion();
    EXPECT_FALSE(rock->IsFrozen());
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 0.0f);
    EXPECT_FLOAT_EQ(rock->ModelPart()->DrawScale().x, 1.0f);
}

TEST(MapObjMotion, LandingNotificationEmitsDustWithoutRigidContacts)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Core::OBB floor;
    floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(*rock);
    for (int step = 0; step < 55; ++step)
    {
        rock->UpdateMotion();
    }
    const NS::Game::Level::LaunchEffects* effects =
        NS::Obj::ComponentCast<NS::Game::Level::LaunchEffects>(rock->Part("LaunchEffects"));
    ASSERT_NE(effects, nullptr);
    int dustCount = 0;
    for (const NS::Game::Player::EffectLayerRecord& record : effects->Layers().Records())
    {
        if (record.name == "launch.landDust")
        {
            ++dustCount;
            EXPECT_GT(record.amount.value_or(0.0f), 0.0f);
        }
    }
    EXPECT_EQ(dustCount, 1);
}

TEST(MapObjMotion, FreezeWithoutSquashPreservesExistingDrawScale)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Obj::Model* mesh = rock->ModelPart();
    ASSERT_NE(mesh, nullptr);
    ASSERT_TRUE(mesh->SnapDrawScale(NS::Core::Vector3{0.9f, 0.9f, 0.9f}));
    NS::Game::Level::TackleFreezeDesc freeze;
    freeze.squash = false;
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleFreeze(*rock, freeze));
    for (int step = 0; step < 4; ++step)
    {
        rock->UpdateMotion();
    }
    EXPECT_FLOAT_EQ(mesh->DrawScale().x, 0.9f);
}

TEST(MapObjMotion, EffectClockIsSeparateFromItsPostMovementUpdate)
{
    NS::Game::Level::LaunchEffects effects;
    effects.BeginStep();
    EXPECT_EQ(effects.Layers().Step(), 1);
    effects.OnUpdate();
    EXPECT_EQ(effects.Layers().Step(), 1);
}

TEST(MapObjMotion, LandingDustStartsOnTheActorUpdateThatLands)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = PlaceMovingRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Core::OBB floor;
    floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
    floor.halfExtentX = 100.0f;
    floor.halfExtentZ = 100.0f;
    scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    ReleaseRock(*rock);
    const NS::Game::Level::LaunchEffects* effects =
        NS::Obj::ComponentCast<NS::Game::Level::LaunchEffects>(rock->Part("LaunchEffects"));
    ASSERT_NE(effects, nullptr);
    bool landed = false;
    for (int step = 0; step < 100 && !landed; ++step)
    {
        rock->Update();
        for (const NS::Game::Player::EffectLayerRecord& record : effects->Layers().Records())
        {
            if (record.name == "launch.landDust")
            {
                EXPECT_EQ(record.startStep, effects->Layers().Step());
                EXPECT_FALSE(rock->IsArc());
                landed = true;
            }
        }
    }
    EXPECT_TRUE(landed);
}
