#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/IUse/IUseEffect.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "TestEffectActor.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"

// エフェクトの関数は窓口だけを受け、個体の枠の保持役へ渡す

namespace
{
    using NS::EffectRecord;
} // namespace

TEST(IUseEffectTest, PlayEffectLeavesOneRecordInTheSlotKeeper)
{
    NS::Scene scene;
    TestEffectActor* spawned = scene.SpawnActor<TestEffectActor>("TestEffectActor");
    ASSERT_NE(spawned, nullptr);
    TestEffectActor& actor = *spawned;
    ASSERT_NE(actor.GetEffectKeeper(), nullptr);

    const NS::uint32 id = NS::PlayEffect(&actor, NS::EffectName{"test.spark"}, NS::Gfx::EffectPlayDesc{});

    EXPECT_NE(id, 0u);
    EXPECT_EQ(actor.GetEffectKeeper()->Records().size(), 1u);
    const EffectRecord* record = NS::FindEffect(&actor, id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->name, "test.spark");
}

TEST(IUseEffectTest, StopEffectWritesTheEndStepFoundByFindEffect)
{
    NS::Scene scene;
    TestEffectActor* spawned = scene.SpawnActor<TestEffectActor>("TestEffectActor");
    ASSERT_NE(spawned, nullptr);
    TestEffectActor& actor = *spawned;
    const NS::uint32 id = NS::PlayEffect(&actor, NS::EffectName{"test.spark"}, NS::Gfx::EffectPlayDesc{});
    actor.Update();
    ASSERT_EQ(NS::GetEffectStep(&actor), 1);

    NS::SetEffectAmount(&actor, id, 3.0f);
    NS::StopEffectRoot(&actor, id);
    NS::StopEffect(&actor, id);

    const EffectRecord* record = NS::FindEffect(&actor, id);
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->amount.has_value());
    EXPECT_FLOAT_EQ(record->amount.value(), 3.0f);
    ASSERT_TRUE(record->rootStopStep.has_value());
    EXPECT_EQ(record->rootStopStep.value(), 1);
    ASSERT_TRUE(record->endStep.has_value());
    EXPECT_EQ(record->endStep.value(), 1);
}

TEST(IUseEffectTest, StopEffectAfterEndsOnTheScheduledStep)
{
    NS::Scene scene;
    TestEffectActor* spawned = scene.SpawnActor<TestEffectActor>("TestEffectActor");
    ASSERT_NE(spawned, nullptr);
    TestEffectActor& actor = *spawned;
    const NS::uint32 id = NS::PlayEffect(&actor, NS::EffectName{"test.spark"}, NS::Gfx::EffectPlayDesc{});
    NS::StopEffectAfter(&actor, id, 2);

    actor.Update();
    EXPECT_FALSE(NS::FindEffect(&actor, id)->endStep.has_value());
    actor.Update();
    EXPECT_TRUE(NS::FindEffect(&actor, id)->endStep.has_value());
}

TEST(IUseEffectTest, ActorWithoutTheSlotDoesNothing)
{
    NS::SceneActor actor;
    ASSERT_EQ(actor.GetEffectKeeper(), nullptr);

    NS::PreloadEffects(&actor, {NS::EffectName{"test.spark"}});
    EXPECT_EQ(NS::PlayEffect(&actor, NS::EffectName{"test.spark"}, NS::Gfx::EffectPlayDesc{}), 0u);
    NS::StopEffect(&actor, 1);
    NS::StopEffectRoot(&actor, 1);
    NS::StopEffectAfter(&actor, 1, 1);
    NS::SetEffectTransform(&actor, 1, NS::Vector3{}, NS::Quaternion::Identity, NS::Vector3{1.0f, 1.0f, 1.0f});
    NS::SetEffectDynamicInput(&actor, 1, 0, 1.0f);
    NS::SetEffectAmount(&actor, 1, 1.0f);
    NS::SetEffectRotation(&actor, 1, NS::Quaternion::Identity);
    EXPECT_EQ(NS::FindEffect(&actor, 1), nullptr);
    EXPECT_EQ(NS::GetEffectStep(&actor), 0);
}
