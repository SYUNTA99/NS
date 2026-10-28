#include "NSlib/CoreObject/ClassObjs/Effect.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "TestEffectActor.h"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "NSlib/Core/Types.h"

// 描画の無い世界の保持役は再生せず、出した記録だけを残す

namespace
{
    class ReactionEffectActor final : public TestEffectActor
    {
    protected:
        void OnInit() override
        {
            TestEffectActor::OnInit();
            CreateClassObj(m_hitReaction);
        }
    };

    using NS::Effect;
    using NS::EffectRecord;
} // namespace

TEST(EffectTest, PlayWithoutAnEffectSceneLeavesOneRecord)
{
    NS::EffectAsset asset{"11111111111111111111111111111111"};
    asset.Replace({}, NS::Gfx::EffectResourceDesc{asset.Id(), "test.spark", {}, 1});
    Effect effect;
    const NS::uint32 id = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});

    EXPECT_NE(id, 0u);
    ASSERT_EQ(effect.Records().size(), 1u);
    const EffectRecord* record = effect.Find(id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->name, "test.spark");
    EXPECT_EQ(record->startStep, 0);
    EXPECT_FALSE(record->endStep.has_value());
    EXPECT_FALSE(record->handle.IsValid());
}

TEST(EffectTest, StopWritesTheStepItEndedOn)
{
    NS::EffectAsset asset{"11111111111111111111111111111111"};
    asset.Replace({}, NS::Gfx::EffectResourceDesc{asset.Id(), "test.spark", {}, 1});
    Effect effect;
    effect.BeginStep(nullptr);
    const NS::uint32 id = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    effect.BeginStep(nullptr);
    effect.StopRoot(nullptr, id);
    effect.BeginStep(nullptr);
    effect.Stop(nullptr, id);

    const EffectRecord* record = effect.Find(id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->startStep, 1);
    ASSERT_TRUE(record->rootStopStep.has_value());
    EXPECT_EQ(record->rootStopStep.value(), 2);
    ASSERT_TRUE(record->endStep.has_value());
    EXPECT_EQ(record->endStep.value(), 3);
}

TEST(EffectTest, StopAfterEndsOnTheScheduledStep)
{
    NS::EffectAsset asset{"11111111111111111111111111111111"};
    asset.Replace({}, NS::Gfx::EffectResourceDesc{asset.Id(), "test.spark", {}, 1});
    Effect effect;
    const NS::uint32 id = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    effect.StopAfter(id, 2);

    effect.BeginStep(nullptr);
    EXPECT_FALSE(effect.Find(id)->endStep.has_value());
    effect.BeginStep(nullptr);
    ASSERT_TRUE(effect.Find(id)->endStep.has_value());
    EXPECT_EQ(effect.Find(id)->endStep.value(), 2);
}

TEST(EffectTest, EndedRecordIsDroppedAfterTheKeepSteps)
{
    NS::EffectAsset asset{"11111111111111111111111111111111"};
    asset.Replace({}, NS::Gfx::EffectResourceDesc{asset.Id(), "test.spark", {}, 1});
    Effect effect{3};
    const NS::uint32 id = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    effect.Stop(nullptr, id);

    for (int step = 0; step < 3; ++step)
    {
        effect.BeginStep(nullptr);
    }
    EXPECT_NE(effect.Find(id), nullptr);
    effect.BeginStep(nullptr);
    EXPECT_EQ(effect.Find(id), nullptr);
}

TEST(EffectTest, AmountAndRotationAreWrittenToTheRecord)
{
    NS::EffectAsset asset{"11111111111111111111111111111111"};
    asset.Replace({}, NS::Gfx::EffectResourceDesc{asset.Id(), "test.spark", {}, 1});
    Effect effect;
    const NS::uint32 id = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    const NS::Quaternion turned = NS::Quaternion::CreateFromAxisAngle(NS::Vector3{0.0f, 1.0f, 0.0f}, 1.0f);
    effect.SetAmount(id, 2.5f);
    effect.SetRotation(id, turned);

    const EffectRecord* record = effect.Find(id);
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->amount.has_value());
    EXPECT_FLOAT_EQ(record->amount.value(), 2.5f);
    ASSERT_TRUE(record->rotation.has_value());
    EXPECT_EQ(record->rotation.value(), turned);
}

TEST(EffectTest, ActorUpdateAdvancesTheSlotKeeperOnce)
{
    NS::Scene scene;
    TestEffectActor* spawned = scene.SpawnActor<TestEffectActor>("TestEffectActor");
    ASSERT_NE(spawned, nullptr);
    TestEffectActor& actor = *spawned;
    Effect* effect = actor.GetEffect();
    ASSERT_NE(effect, nullptr);
    ASSERT_EQ(effect->Step(), 0);

    actor.Update();
    EXPECT_EQ(effect->Step(), 1);
}

TEST(EffectTest, SlotKeeperIsOrderedAfterHitReaction)
{
    const std::unique_ptr<ReactionEffectActor> owned = NS::CreateActor<ReactionEffectActor>();
    ReactionEffectActor& actor = *owned;
    NS::Object* effect = actor.GetEffect();
    NS::Object* reaction = actor.HitReactionClassObj();
    ASSERT_NE(effect, nullptr);
    ASSERT_NE(reaction, nullptr);

    std::vector<NS::Object*> parts;
    std::vector<std::string_view> keys;
    NS::ForEachReflectedClassObj(&actor, [&](const NS::ClassObjInfo& field, NS::Object* part) {
        parts.push_back(part);
        keys.push_back(field.key);
    });
    ASSERT_EQ(parts.size(), 4u);
    EXPECT_EQ(parts[2], reaction);
    EXPECT_EQ(parts[3], effect);
    EXPECT_EQ(keys[3], "Effect");
}
