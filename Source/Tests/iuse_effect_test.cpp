#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseEffect.h"
#include "NSlib/Object/SubObjects/EffectKeeper.h"

#include <gtest/gtest.h>

#include <cstdint>

// エフェクトの関数は窓口だけを受け、個体の枠の保持役へ渡す

namespace
{
    using NS::Obj::EffectRecord;
} // namespace

TEST(IUseEffectTest, PlayEffectLeavesOneRecordInTheSlotKeeper)
{
    NS::Obj::Actor actor;
    ASSERT_NE(actor.CreateSubObj("Effect"), nullptr);
    ASSERT_NE(actor.GetEffectKeeper(), nullptr);

    const std::uint32_t id = NS::Obj::PlayEffect(&actor, "test.spark", NS::Gfx::EffectPlayDesc{});

    EXPECT_NE(id, 0u);
    EXPECT_EQ(actor.GetEffectKeeper()->Records().size(), 1u);
    const EffectRecord* record = NS::Obj::FindEffect(&actor, id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->name, "test.spark");
}

TEST(IUseEffectTest, StopEffectWritesTheEndStepFoundByFindEffect)
{
    NS::Obj::Actor actor;
    ASSERT_NE(actor.CreateSubObj("Effect"), nullptr);
    const std::uint32_t id = NS::Obj::PlayEffect(&actor, "test.spark", NS::Gfx::EffectPlayDesc{});
    actor.Update();
    ASSERT_EQ(NS::Obj::EffectStep(&actor), 1);

    NS::Obj::SetEffectAmount(&actor, id, 3.0f);
    NS::Obj::StopEffectRoot(&actor, id);
    NS::Obj::StopEffect(&actor, id);

    const EffectRecord* record = NS::Obj::FindEffect(&actor, id);
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
    NS::Obj::Actor actor;
    ASSERT_NE(actor.CreateSubObj("Effect"), nullptr);
    const std::uint32_t id = NS::Obj::PlayEffect(&actor, "test.spark", NS::Gfx::EffectPlayDesc{});
    NS::Obj::StopEffectAfter(&actor, id, 2);

    actor.Update();
    EXPECT_FALSE(NS::Obj::FindEffect(&actor, id)->endStep.has_value());
    actor.Update();
    EXPECT_TRUE(NS::Obj::FindEffect(&actor, id)->endStep.has_value());
}

TEST(IUseEffectTest, ActorWithoutTheSlotDoesNothing)
{
    NS::Obj::Actor actor;
    ASSERT_EQ(actor.GetEffectKeeper(), nullptr);

    NS::Obj::PreloadEffects(&actor, {"test.spark"});
    EXPECT_EQ(NS::Obj::PlayEffect(&actor, "test.spark", NS::Gfx::EffectPlayDesc{}), 0u);
    NS::Obj::StopEffect(&actor, 1);
    NS::Obj::StopEffectRoot(&actor, 1);
    NS::Obj::StopEffectAfter(&actor, 1, 1);
    NS::Obj::SetEffectTransform(&actor, 1, NS::Vector3{}, NS::Quaternion::Identity, NS::Vector3{1.0f, 1.0f, 1.0f});
    NS::Obj::SetEffectDynamicInput(&actor, 1, 0, 1.0f);
    NS::Obj::SetEffectAmount(&actor, 1, 1.0f);
    NS::Obj::SetEffectRotation(&actor, 1, NS::Quaternion::Identity);
    EXPECT_EQ(NS::Obj::FindEffect(&actor, 1), nullptr);
    EXPECT_EQ(NS::Obj::EffectStep(&actor), 0);
}
