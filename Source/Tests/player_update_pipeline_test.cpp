#include "Game/Level/CollisionInput.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"

#include <gtest/gtest.h>

namespace
{
    Player* PlacePipelinePlayer(NS::Obj::Scene& scene, float targetX = 0.0f, float targetZ = 3.0f)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{targetX, 0.5f, targetZ});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        NS::Core::OBB floor{};
        floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        scene.MainCamera()->SetPosition(NS::Core::Vector3{});
        scene.MainCamera()->SetTarget(NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    void LegacyPipeline(Player& player, bool held)
    {
        player.ChargeControl().Step(held, NS::Platform::FrameTimer::FixedDelta());
        player.Resolver().OnUpdate();
        player.ChargeControl().SetActive(false);
        player.Resolver().SetActive(false);
        player.Update(false);
        player.ChargeControl().SetActive(true);
        player.Resolver().SetActive(true);
    }

    void ExpectSameVector(const NS::Core::Vector3& actual, const NS::Core::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 0.00001f);
        EXPECT_NEAR(actual.y, expected.y, 0.00001f);
        EXPECT_NEAR(actual.z, expected.z, 0.00001f);
    }
} // namespace

TEST(PlayerUpdatePipeline, ObservationDoesNotAdvanceChargeOrMoveThePlayer)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Body().SetVelocity(NS::Core::Vector3{2.0f, 4.0f, 3.0f});
    const NS::Core::Vector3 position = player->Root().Position();
    const NS::Core::Vector3 velocity = player->Body().Velocity();
    player->ChargeControl().Observe(true);
    player->Resolver().ObserveImpact(player->ChargeControl().PredictedSlamVelocity());
    EXPECT_FALSE(player->ChargeControl().Judge().IsHeld());
    EXPECT_FALSE(player->IsCurled());
    EXPECT_EQ(player->Resolver().LastImpact().sequence, 0u);
    ExpectSameVector(player->Root().Position(), position);
    ExpectSameVector(player->Body().Velocity(), velocity);
    player->ChargeControl().AdvanceState(NS::Platform::FrameTimer::FixedDelta());
    EXPECT_TRUE(player->ChargeControl().Judge().JustPressed());
    EXPECT_TRUE(player->IsCurled());
}

TEST(PlayerUpdatePipeline, OneObservationCannotAdvanceChargeTwice)
{
    Player player;
    player.ChargeControl().OnStart();
    player.ChargeControl().Observe(true);
    player.ChargeControl().AdvanceState(0.1f);
    player.ChargeControl().AdvanceState(0.1f);
    EXPECT_TRUE(player.ChargeControl().Judge().JustPressed());
    EXPECT_FALSE(player.ChargeControl().IsCharging());
    player.ChargeControl().Observe(true);
    player.ChargeControl().AdvanceState(0.1f);
    EXPECT_TRUE(player.ChargeControl().Judge().JustStartedCharging());
}

// 左右の寄せは消した。脇の相手へ向きを曲げると、放った後に矢印とずれて当て所が見えない所で動く
TEST(PlayerUpdatePipeline, SlamHeadingStaysOnTheAimBesideAnOffAxisTarget)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.5f, 3.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    const NS::Core::Vector3 before = player->BodySlamVelocity();
    player->ChargeControl().Observe(false);
    const NS::Core::Vector3 predicted = player->ChargeControl().PredictedSlamVelocity();
    ExpectSameVector(predicted, before);
    ExpectSameVector(player->BodySlamVelocity(), before);
    player->ChargeControl().AdvanceState(NS::Platform::FrameTimer::FixedDelta());
    player->States().Step(*player, NS::Platform::FrameTimer::FixedDelta());
    EXPECT_FLOAT_EQ(player->BodySlamVelocity().x, before.x);
    const float vertical = player->Body().VerticalVelocity();
    player->ChargeControl().ApplyControl();
    EXPECT_NEAR(player->Body().Velocity().x, predicted.x, 0.00001f);
    EXPECT_NEAR(player->Body().Velocity().z, predicted.z, 0.00001f);
    EXPECT_FLOAT_EQ(player->Body().VerticalVelocity(), vertical);
    const NS::Core::Vector3 once = player->BodySlamVelocity();
    player->ChargeControl().ApplyControl();
    ExpectSameVector(player->BodySlamVelocity(), once);
}

TEST(PlayerUpdatePipeline, ActorPipelineMatchesLegacyChargeImpactFreezeAndReleaseFrames)
{
    NS::Obj::Scene referenceScene;
    NS::Obj::Scene actualScene;
    Player* reference = PlacePipelinePlayer(referenceScene, 0.04f, 2.5f);
    Player* actual = PlacePipelinePlayer(actualScene, 0.04f, 2.5f);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(actual, nullptr);
    bool sawImpact = false;
    bool sawFreeze = false;
    bool sawRelease = false;
    for (int frame = 0; frame < 190; ++frame)
    {
        SCOPED_TRACE(frame);
        const bool held = frame < 90;
        if (held)
        {
            reference->Body().SetGrounded(true);
            actual->Body().SetGrounded(true);
        }
        if (frame == 92)
        {
            referenceScene.Objects().FindByObjectId(2)->Root().ShiftPosition(NS::Core::Vector3{0.1f, 0.0f, 0.0f});
            actualScene.Objects().FindByObjectId(2)->Root().ShiftPosition(NS::Core::Vector3{0.1f, 0.0f, 0.0f});
        }
        LegacyPipeline(*reference, held);
        actual->Update(held);
        ExpectSameVector(actual->Root().Position(), reference->Root().Position());
        ExpectSameVector(actual->Body().Velocity(), reference->Body().Velocity());
        ExpectSameVector(actual->Root().Scale(), reference->Root().Scale());
        EXPECT_EQ(actual->States().CurrentId(), reference->States().CurrentId());
        EXPECT_EQ(actual->Body().IsActive(), reference->Body().IsActive());
        EXPECT_EQ(actual->Resolver().LastImpact().sequence, reference->Resolver().LastImpact().sequence);
        EXPECT_EQ(actual->Resolver().FreezeBeganThisStep(), reference->Resolver().FreezeBeganThisStep());
        EXPECT_EQ(actual->Resolver().ReleasedThisStep(), reference->Resolver().ReleasedThisStep());
        sawImpact = sawImpact || actual->Resolver().LastImpact().sequence > 0;
        sawFreeze = sawFreeze || actual->Resolver().FreezeBeganThisStep();
        sawRelease = sawRelease || actual->Resolver().ReleasedThisStep();
        NS::Game::Level::MapObj* referenceRock =
            NS::Obj::Cast<NS::Game::Level::MapObj>(referenceScene.Objects().FindByObjectId(2));
        NS::Game::Level::MapObj* actualRock =
            NS::Obj::Cast<NS::Game::Level::MapObj>(actualScene.Objects().FindByObjectId(2));
        ASSERT_NE(referenceRock, nullptr);
        ASSERT_NE(actualRock, nullptr);
        referenceRock->Update();
        actualRock->Update();
        ExpectSameVector(actualRock->Root().Position(), referenceRock->Root().Position());
    }
    EXPECT_TRUE(sawImpact);
    EXPECT_TRUE(sawFreeze);
    EXPECT_TRUE(sawRelease);
}

TEST(PlayerUpdatePipeline, OneObservationCannotBeginFreezeTwice)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Resolver().ObserveImpact(player->BodySlamVelocity());
    player->Resolver().StepState();
    ASSERT_EQ(player->Resolver().LastImpact().sequence, 1u);
    ASSERT_FALSE(player->Resolver().FreezeBeganThisStep());
    player->Resolver().StepState();
    EXPECT_FALSE(player->Resolver().FreezeBeganThisStep());
    EXPECT_TRUE(player->Body().IsActive());
    player->Resolver().ObserveImpact(player->BodySlamVelocity());
    player->Resolver().StepState();
    EXPECT_TRUE(player->Resolver().FreezeBeganThisStep());
    EXPECT_FALSE(player->Body().IsActive());
}

TEST(PlayerUpdatePipeline, RemovingTheObservedTargetCannotApplyAStaleImpact)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.0f, 0.6f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Resolver().ObserveImpact(player->BodySlamVelocity());
    scene.Objects().RemoveByObjectId(2);
    player->Resolver().StepState();
    EXPECT_EQ(player->Resolver().LastImpact().sequence, 0u);
    EXPECT_TRUE(player->IsBodySlamming());
    EXPECT_TRUE(player->Body().IsActive());
}

TEST(PlayerUpdatePipeline, PausedMovementKeepsItsStoredVelocityDuringSlamControl)
{
    NS::Obj::Scene scene;
    Player* player = PlacePipelinePlayer(scene, 0.5f, 3.0f);
    ASSERT_NE(player, nullptr);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(player->BodySlam());
    player->Body().SetVelocity(NS::Core::Vector3{0.0f, 2.0f, 0.0f});
    player->Body().SetActive(false);
    const NS::Core::Vector3 position = player->Root().Position();
    const NS::Core::Vector3 velocity = player->Body().Velocity();
    const std::uint32_t stateStep = player->States().StateStep();
    player->Update(false);
    ExpectSameVector(player->Root().Position(), position);
    ExpectSameVector(player->Body().Velocity(), velocity);
    EXPECT_EQ(player->States().StateStep(), stateStep);
}

TEST(PlayerUpdatePipeline, ActorPipelineMatchesLegacyTapFrames)
{
    NS::Obj::Scene referenceScene;
    NS::Obj::Scene actualScene;
    Player* reference = PlacePipelinePlayer(referenceScene);
    Player* actual = PlacePipelinePlayer(actualScene);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(actual, nullptr);
    bool sawTap = false;
    for (int frame = 0; frame < 90; ++frame)
    {
        SCOPED_TRACE(frame);
        const bool held = frame < 3;
        LegacyPipeline(*reference, held);
        actual->Update(held);
        ExpectSameVector(actual->Root().Position(), reference->Root().Position());
        ExpectSameVector(actual->Body().Velocity(), reference->Body().Velocity());
        EXPECT_EQ(actual->States().CurrentId(), reference->States().CurrentId());
        EXPECT_EQ(actual->Resolver().LastImpact().sequence, reference->Resolver().LastImpact().sequence);
        EXPECT_EQ(actual->Resolver().FreezeBeganThisStep(), reference->Resolver().FreezeBeganThisStep());
        EXPECT_EQ(actual->Resolver().ReleasedThisStep(), reference->Resolver().ReleasedThisStep());
        if (actual->IsBodySlamming())
        {
            sawTap = true;
            EXPECT_FLOAT_EQ(actual->BodySlamCharge01(), 0.0f);
        }
    }
    EXPECT_TRUE(sawTap);
}

TEST(PlayerUpdatePipeline, StateTransitionPreservesChargeAndTapTrajectories)
{
    const nlohmann::json baseline = nlohmann::json::parse(
        R"([
        [0, 94, 95, 107, [
            [0, 0, 0.649999976, 0, 0, 0, 0, 0.04, 0.5, 2.5],
            [90, 0, 0.6500000358, 0.3333333433, 0, 0, 20, 0.04, 0.5, 2.5],
            [92, 0, 0.6499999762, 1.0, 0, 0, 20, 0.14, 0.5, 2.5],
            [93, 0, 0.6499999166, 1.3333333731, 0, 0, 20, 0.14, 0.5, 2.5],
            [100, -0.0123793595, 0.6500000954, 1.3523943424, -0.8675079346, 1.67509e-7, 0.104101181, 0.14, 0.5, 2.5454165936],
            [110, -0.02943230793, 1.116387725, 1.210286379, -0.2557942271, 6.683314323, -2.131618738, 0.14, 1.384893417, 5.983239174],
            [140, -0.1573294252, 2.848669291, 0.1444766968, -0.2557942271, 0.641646266, -2.131618738, 0.14, 4.491162777, 32.10754395],
            [189, -0.294336319, 1.149999738, -0.9972489476, 0, 0, 0, 0.14, 0.501000941, 74.210495]
        ]],
        [1, 14, 15, 18, [
            [0, 0, 0.649999976, 0, 0, 0, 0, 0, 0.5, 3],
            [3, 0, 0.6973332763, 0.1666666716, 0, 2.839999914, 10, 0, 0.5, 3],
            [4, 0, 0.7419999242, 0.3333333433, 0, 2.679999828, 10, 0, 0.5, 3],
            [10, 0, 0.953999877, 1.333333254, 0, 1.719999552, 10, 0, 0.5, 3],
            [30, 0, 1.669279456, 1.836697578, 0, 1.664880991, -0.5998571515, 0, 1.712962747, 9.446973801],
            [60, 0, 1.149999976, 1.53676939, 0, 0, -0.5998571515, 0, 0.5009999871, 24.31210518],
            [89, 0, 1.149999738, 1.53676939, 0, 0, 0, 0, 0.500999987, 36.63962555]
        ]]
    ])");
    for (int scenario = 0; scenario < 2; ++scenario)
    {
        NS::Obj::Scene scene;
        const bool charge = scenario == 0;
        float targetX = 0.0f;
        float targetZ = 3.0f;
        int frames = 90;
        int holdFrames = 3;
        if (charge)
        {
            targetX = 0.04f;
            targetZ = 2.5f;
            frames = 190;
            holdFrames = 90;
        }
        Player* player = PlacePipelinePlayer(scene, targetX, targetZ);
        ASSERT_NE(player, nullptr);
        NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
        ASSERT_NE(rock, nullptr);
        int impact = -1;
        int freeze = -1;
        int release = -1;
        const nlohmann::json& expected = baseline[scenario];
        for (int frame = 0; frame < frames; ++frame)
        {
            const bool held = frame < holdFrames;
            if (held)
            {
                player->Body().SetGrounded(true);
            }
            if (charge && frame == 92)
            {
                rock->Root().ShiftPosition(NS::Core::Vector3{0.1f, 0.0f, 0.0f});
            }
            player->Update(held);
            if (impact < 0 && player->Resolver().LastImpact().sequence > 0)
            {
                impact = frame;
            }
            if (freeze < 0 && player->Resolver().FreezeBeganThisStep())
            {
                freeze = frame;
            }
            if (release < 0 && player->Resolver().ReleasedThisStep())
            {
                release = frame;
            }
            rock->Update();
            for (const nlohmann::json& sample : expected[4])
            {
                if (sample[0].get<int>() != frame)
                {
                    continue;
                }
                SCOPED_TRACE(scenario);
                SCOPED_TRACE(frame);
                ExpectSameVector(
                    player->Root().Position(),
                    NS::Core::Vector3{sample[1].get<float>(), sample[2].get<float>(), sample[3].get<float>()});
                ExpectSameVector(
                    player->Body().Velocity(),
                    NS::Core::Vector3{sample[4].get<float>(), sample[5].get<float>(), sample[6].get<float>()});
                ExpectSameVector(
                    rock->Root().Position(),
                    NS::Core::Vector3{sample[7].get<float>(), sample[8].get<float>(), sample[9].get<float>()});
            }
        }
        EXPECT_EQ(impact, expected[1].get<int>());
        EXPECT_EQ(freeze, expected[2].get<int>());
        EXPECT_EQ(release, expected[3].get<int>());
    }
}
