#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Game/Player/PlayerAppearance.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Graphics/Animation.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Object/SubObjects/Animation.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Windows/Clock.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace
{
    void PlaceBodies(NS::Obj::Scene& scene, bool withPlayer)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 1);
        NS::Obj::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 20.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        if (withPlayer)
        {
            nlohmann::json player = NS::Obj::MakeObjectJson();
            NS::Obj::SetObjectJsonClass(player, "Player");
            NS::Obj::SetObjectJsonId(player, 2);
            NS::Obj::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
            NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        }
        scene.LoadJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
    }

    GL::Level::MapObj* RockOf(NS::Obj::Scene& scene)
    {
        return NS::Obj::Cast<GL::Level::MapObj>(scene.Objects().FindByObjectId(1));
    }

    Player* PlayerOf(NS::Obj::Scene& scene)
    {
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(2));
    }

    void ReleaseRock(GL::Level::MapObj& rock)
    {
        GL::Level::TackleReleaseDesc release;
        release.arc.distance = 5.0f;
        release.arc.apexHeight = 1.0f;
        ASSERT_TRUE(GL::Level::SendMsgTackleRelease(rock, release));
    }

    struct SlamStep
    {
        NS::Vector3 moved;
        float spinDegrees = 0.0f;
    };

    SlamStep SecondSlamStep(Player& player)
    {
        player.RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        player.Update(false);
        const NS::Vector3 from = player.Root().Position();
        player.Update(false);
        SlamStep step;
        step.moved = player.Root().Position() - from;
        step.spinDegrees = player.Appearance().SpinDegreesThisFrame();
        return step;
    }
} // namespace

TEST(ActorBodyTime, ScaleOneGivesTheFixedStep)
{
    NS::Obj::Actor actor;
    EXPECT_FLOAT_EQ(actor.BodyTimeScale(), 1.0f);
    EXPECT_EQ(actor.BodyDelta(), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, RockAtScaleTwoFliesTwoStepsInOne)
{
    NS::Obj::Scene normal;
    NS::Obj::Scene fast;
    PlaceBodies(normal, false);
    PlaceBodies(fast, true);
    GL::Level::MapObj* slowRock = RockOf(normal);
    GL::Level::MapObj* fastRock = RockOf(fast);
    Player* player = PlayerOf(fast);
    ASSERT_NE(slowRock, nullptr);
    ASSERT_NE(fastRock, nullptr);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(fastRock->SetBodyTimeScale(2.0f));
    ReleaseRock(*slowRock);
    ReleaseRock(*fastRock);
    for (int step = 0; step < 4; ++step)
    {
        slowRock->UpdateMotion();
        slowRock->UpdateMotion();
        fastRock->UpdateMotion();
        const NS::Vector3 slow = slowRock->Root().Position();
        const NS::Vector3 fastPosition = fastRock->Root().Position();
        EXPECT_NEAR(fastPosition.x, slow.x, 1.0e-3f) << step;
        EXPECT_NEAR(fastPosition.y, slow.y, 1.0e-3f) << step;
        EXPECT_NEAR(fastPosition.z, slow.z, 1.0e-3f) << step;
    }
    EXPECT_GT(slowRock->Root().Position().x, 0.5f);
    EXPECT_EQ(player->BodyDelta(), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, PlayerAtScaleTwoMovesAndSpinsTwoStepsInOne)
{
    NS::Obj::Scene normal;
    NS::Obj::Scene fast;
    PlaceBodies(normal, true);
    PlaceBodies(fast, true);
    Player* slowPlayer = PlayerOf(normal);
    Player* fastPlayer = PlayerOf(fast);
    ASSERT_NE(slowPlayer, nullptr);
    ASSERT_NE(fastPlayer, nullptr);
    ASSERT_TRUE(fastPlayer->SetBodyTimeScale(2.0f));
    const SlamStep slow = SecondSlamStep(*slowPlayer);
    const SlamStep fastStep = SecondSlamStep(*fastPlayer);
    ASSERT_GT(slow.moved.z, 0.0f);
    ASSERT_GT(slow.spinDegrees, 0.0f);
    EXPECT_NEAR(fastStep.moved.z, slow.moved.z * 2.0f, slow.moved.z * 0.01f);
    EXPECT_NEAR(fastStep.spinDegrees, slow.spinDegrees * 2.0f, slow.spinDegrees * 0.01f);
    GL::Level::MapObj* rock = RockOf(fast);
    ASSERT_NE(rock, nullptr);
    EXPECT_EQ(rock->BodyDelta(), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, PlayerAtScaleTwoAdvancesAnimationTwoSteps)
{
    // アニメはクリップを写さずに指すので、試しの間は生かしておく
    const std::vector<NS::Gfx::AnimationClip> clips = {
        {.name = "idle", .duration = 10.0f}, {.name = "run", .duration = 10.0f}, {.name = "walk", .duration = 10.0f}};
    float times[2] = {};
    for (int index = 0; index < 2; ++index)
    {
        Player player;
        player.Init();
        NS::Obj::Animation* animation = NS::Obj::Cast<NS::Obj::Animation>(player.CreateSubObj("Animation"));
        ASSERT_NE(animation, nullptr);
        animation->AddClips(clips);
        ASSERT_TRUE(player.SetBodyTimeScale(static_cast<float>(index + 1)));
        player.Body().SetGrounded(true);
        player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
        player.UpdateAnimation();
        animation->OnUpdate();
        times[index] = animation->Time();
    }
    ASSERT_GT(times[0], 0.0f);
    EXPECT_NEAR(times[1], times[0] * 2.0f, 1.0e-6f);
}

TEST(ActorBodyTime, SubObjectReadsItsOwnersClock)
{
    NS::Obj::Actor actor;
    actor.Init();
    NS::Obj::SubObject* animation = actor.CreateSubObj("Animation");
    ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(actor.SetBodyTimeScale(1.5f));
    EXPECT_EQ(animation->BodyDelta(), actor.BodyDelta());

    const NS::Obj::Animation loose;
    EXPECT_EQ(loose.BodyDelta(), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, RejectsScalesThatAreNotFiniteAndPositive)
{
    NS::Obj::Actor actor;
    ASSERT_TRUE(actor.SetBodyTimeScale(2.0f));
    for (const float bad :
         {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        EXPECT_FALSE(actor.SetBodyTimeScale(bad)) << bad;
        EXPECT_FLOAT_EQ(actor.BodyTimeScale(), 2.0f) << bad;
    }
}
