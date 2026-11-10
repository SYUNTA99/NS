#include "Editor/EditorActors.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "TestAnimatedPlayerActor.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "TestLogCapture.h"
#include "TestSpawn.h"
#include "Tests/TestSceneSetup.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace
{
    // 身体の段ごとに、自分の体の秒 (GetFrameDelta) × 一定の速さだけ +x へ進む
    class ConstantSpeedActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(ConstantSpeedActor, NS::SceneActor)

        static constexpr float k_Speed = 6.0f;

    protected:
        void BodyStep() override { Root().ShiftPosition(NS::Vector3{k_Speed * NS::GetFrameDelta(this), 0.0f, 0.0f}); }
    };

    void PlaceBodies(NS::Scene& scene, bool withPlayer)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        if (withPlayer)
        {
            AddActorJson(doc, "PlayerActor", 2, NS::Vector3{0.0f, 1.0f, 0.0f});
        }
        scene.ResetFromJson(doc);
        AddTerrainBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, 100.0f, 0.5f, 100.0f);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
    }

    PlayerActor* PlayerOf(NS::Scene& scene)
    {
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(2));
    }
} // namespace

NS_CLASS(ConstantSpeedActor)
NS_CLASS(TestAnimatedPlayerActor)

TEST(ActorBodyTime, ScaleOneGivesTheFixedStep)
{
    NS::SceneActor actor;
    EXPECT_FLOAT_EQ(NS::GetFrameRate(&actor), 1.0f);
    EXPECT_EQ(NS::GetFrameDelta(&actor), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, ActorAtScaleTwoMovesTwoStepsInOne)
{
    NS::Scene normal;
    NS::Scene fast;
    PlaceBodies(normal, false);
    PlaceBodies(fast, true);
    ConstantSpeedActor* slowActor = SpawnFromActorFile<ConstantSpeedActor>(normal, "ConstantSpeedActor");
    ConstantSpeedActor* fastActor = SpawnFromActorFile<ConstantSpeedActor>(fast, "ConstantSpeedActor");
    PlayerActor* player = PlayerOf(fast);
    ASSERT_NE(slowActor, nullptr);
    ASSERT_NE(fastActor, nullptr);
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(NS::SetFrameRate(fastActor, 2.0f));
    for (int step = 0; step < 4; ++step)
    {
        slowActor->Update();
        slowActor->Update();
        fastActor->Update();
        const NS::Vector3 slow = slowActor->Root().Position();
        const NS::Vector3 fastPosition = fastActor->Root().Position();
        EXPECT_NEAR(fastPosition.x, slow.x, 1.0e-3f) << step;
        EXPECT_NEAR(fastPosition.y, slow.y, 1.0e-3f) << step;
        EXPECT_NEAR(fastPosition.z, slow.z, 1.0e-3f) << step;
    }
    EXPECT_GT(slowActor->Root().Position().x, 0.5f);
    EXPECT_EQ(NS::GetFrameDelta(player), NS::OS::FrameTimer::FixedDelta());
}

TEST(ActorBodyTime, PlayerAtScaleTwoAdvancesAnimationTwoSteps)
{
    float times[2] = {};
    for (int index = 0; index < 2; ++index)
    {
        // 回ごとにシーンを分け、前の回の自機の状態を持ち越さない
        const ScopedPlayerActionClips clips({{"Run", "Assets/Models/Xbot.glb#run"}});
        NS::Scene scene;
        PlayerActor* spawned = SpawnTestAnimatedPlayer(scene);
        ASSERT_NE(spawned, nullptr);
        PlayerActor& player = *spawned;
        NS::Animation* animation = player.AnimationClassObj();
        ASSERT_NE(animation, nullptr);
        ASSERT_TRUE(NS::SetFrameRate(&player, static_cast<float>(index + 1)));
        player.Body().SetGrounded(true);
        player.Body().SetLateralVelocity(NS::Vector3{4.0f, 0.0f, 0.0f});
        player.UpdateAnimation();
        player.PrepareRender();
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
