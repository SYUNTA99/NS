#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Clock.h"
#include "TestAnimatedPlayerActor.h"
#include "TestLogCapture.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace
{
    void PlaceBodies(NS::Scene& scene, bool withPlayer)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(rock, "PropActor");
        NS::SetObjectJsonId(rock, 1);
        NS::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 20.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        if (withPlayer)
        {
            nlohmann::json player = NS::MakeObjectJson();
            NS::SetObjectJsonActorFile(player, "PlayerActor");
            NS::SetObjectJsonId(player, 2);
            NS::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
            NS::SceneJsonObjects(doc).push_back(std::move(player));
        }
        scene.ResetFromJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
    }

    GL::Level::PropActor* RockOf(NS::Scene& scene)
    {
        return NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(1));
    }

    PlayerActor* PlayerOf(NS::Scene& scene)
    {
        return NS::Cast<PlayerActor>(scene.Objects().FindByObjectId(2));
    }

    void ReleaseRock(GL::Level::PropActor& rock)
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

    SlamStep SecondSlamStep(PlayerActor& player)
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
    NS::SceneActor actor;
    EXPECT_FLOAT_EQ(NS::GetFrameRate(&actor), 1.0f);
    EXPECT_EQ(NS::GetFrameDelta(&actor), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, RockAtScaleTwoFliesTwoStepsInOne)
{
    NS::Scene normal;
    NS::Scene fast;
    PlaceBodies(normal, false);
    PlaceBodies(fast, true);
    GL::Level::PropActor* slowRock = RockOf(normal);
    GL::Level::PropActor* fastRock = RockOf(fast);
    PlayerActor* player = PlayerOf(fast);
    ASSERT_NE(slowRock, nullptr);
    ASSERT_NE(fastRock, nullptr);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(NS::SetFrameRate(fastRock, 2.0f));
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
    EXPECT_EQ(NS::GetFrameDelta(player), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, PlayerAtScaleTwoMovesAndSpinsTwoStepsInOne)
{
    NS::Scene normal;
    NS::Scene fast;
    PlaceBodies(normal, true);
    PlaceBodies(fast, true);
    PlayerActor* slowPlayer = PlayerOf(normal);
    PlayerActor* fastPlayer = PlayerOf(fast);
    ASSERT_NE(slowPlayer, nullptr);
    ASSERT_NE(fastPlayer, nullptr);
    ASSERT_TRUE(NS::SetFrameRate(fastPlayer, 2.0f));
    const SlamStep slow = SecondSlamStep(*slowPlayer);
    const SlamStep fastStep = SecondSlamStep(*fastPlayer);
    ASSERT_GT(slow.moved.z, 0.0f);
    ASSERT_GT(slow.spinDegrees, 0.0f);
    EXPECT_NEAR(fastStep.moved.z, slow.moved.z * 2.0f, slow.moved.z * 0.01f);
    EXPECT_NEAR(fastStep.spinDegrees, slow.spinDegrees * 2.0f, slow.spinDegrees * 0.01f);
    GL::Level::PropActor* rock = RockOf(fast);
    ASSERT_NE(rock, nullptr);
    EXPECT_EQ(NS::GetFrameDelta(rock), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, PlayerAtScaleTwoAdvancesAnimationTwoSteps)
{
    float times[2] = {};
    for (int index = 0; index < 2; ++index)
    {
        const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<TestAnimatedPlayerActor>();
        PlayerActor& player = *playerOwned;
        NS::Animation* animation = player.AnimationClassObj();
        ASSERT_NE(animation, nullptr);
        ASSERT_EQ(UseTestClips(player, {{"走りのクリップ", "Assets/Models/Xbot.glb#run"}}), 0u);
        ASSERT_TRUE(NS::SetFrameRate(&player, static_cast<float>(index + 1)));
        player.Body().SetGrounded(true);
        player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
        player.UpdateAnimation();
        animation->Update();
        times[index] = animation->Time();
    }
    ASSERT_GT(times[0], 0.0f);
    EXPECT_NEAR(times[1], times[0] * 2.0f, 1.0e-6f);
}

TEST(ActorBodyTime, RejectsScalesThatAreNotFiniteAndPositive)
{
    NS::SceneActor actor;
    ASSERT_TRUE(NS::SetFrameRate(&actor, 2.0f));
    const TestLogCapture log;
    for (const float bad :
         {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        EXPECT_FALSE(NS::SetFrameRate(&actor, bad)) << bad;
        EXPECT_FLOAT_EQ(NS::GetFrameRate(&actor), 2.0f) << bad;
    }
    EXPECT_EQ(log.ErrorCount(), 4);
}
