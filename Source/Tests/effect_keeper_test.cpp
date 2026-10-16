#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/EffectKeeper.h"
#include "NSlib/CoreObject/SubObjects/HitReaction.h"
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
            CreateSubObj(m_hitReaction, this);
        }
    };

    using NS::EffectKeeper;
    using NS::EffectRecord;
} // namespace

TEST(EffectKeeperTest, PlayWithoutAnOwnerLeavesOneRecord)
{
    EffectKeeper keeper{nullptr};
    const NS::uint32 id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});

    EXPECT_NE(id, 0u);
    ASSERT_EQ(keeper.Records().size(), 1u);
    const EffectRecord* record = keeper.Find(id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->name, "test.spark");
    EXPECT_EQ(record->startStep, 0);
    EXPECT_FALSE(record->endStep.has_value());
    EXPECT_FALSE(record->handle.IsValid());
}

TEST(EffectKeeperTest, StopWritesTheStepItEndedOn)
{
    EffectKeeper keeper{nullptr};
    keeper.BeginStep();
    const NS::uint32 id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
    keeper.BeginStep();
    keeper.StopRoot(id);
    keeper.BeginStep();
    keeper.Stop(id);

    const EffectRecord* record = keeper.Find(id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->startStep, 1);
    ASSERT_TRUE(record->rootStopStep.has_value());
    EXPECT_EQ(record->rootStopStep.value(), 2);
    ASSERT_TRUE(record->endStep.has_value());
    EXPECT_EQ(record->endStep.value(), 3);
}

TEST(EffectKeeperTest, StopAfterEndsOnTheScheduledStep)
{
    EffectKeeper keeper{nullptr};
    const NS::uint32 id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
    keeper.StopAfter(id, 2);

    keeper.BeginStep();
    EXPECT_FALSE(keeper.Find(id)->endStep.has_value());
    keeper.BeginStep();
    ASSERT_TRUE(keeper.Find(id)->endStep.has_value());
    EXPECT_EQ(keeper.Find(id)->endStep.value(), 2);
}

TEST(EffectKeeperTest, EndedRecordIsDroppedAfterTheKeepSteps)
{
    EffectKeeper keeper{nullptr, 3};
    const NS::uint32 id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
    keeper.Stop(id);

    for (int step = 0; step < 3; ++step)
    {
        keeper.BeginStep();
    }
    EXPECT_NE(keeper.Find(id), nullptr);
    keeper.BeginStep();
    EXPECT_EQ(keeper.Find(id), nullptr);
}

TEST(EffectKeeperTest, AmountAndRotationAreWrittenToTheRecord)
{
    EffectKeeper keeper{nullptr};
    const NS::uint32 id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
    const NS::Quaternion turned = NS::Quaternion::CreateFromAxisAngle(NS::Vector3{0.0f, 1.0f, 0.0f}, 1.0f);
    keeper.SetAmount(id, 2.5f);
    keeper.SetRotation(id, turned);

    const EffectRecord* record = keeper.Find(id);
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->amount.has_value());
    EXPECT_FLOAT_EQ(record->amount.value(), 2.5f);
    ASSERT_TRUE(record->rotation.has_value());
    EXPECT_EQ(record->rotation.value(), turned);
}

TEST(EffectKeeperTest, ActorUpdateAdvancesTheSlotKeeperOnce)
{
    const std::unique_ptr<TestEffectActor> owned = NS::CreateActor<TestEffectActor>();
    TestEffectActor& actor = *owned;
    EffectKeeper* keeper = actor.GetEffectKeeper();
    ASSERT_NE(keeper, nullptr);
    ASSERT_EQ(keeper->Step(), 0);

    actor.Update();
    EXPECT_EQ(keeper->Step(), 1);
}

TEST(EffectKeeperTest, SlotKeeperIsOrderedAfterHitReaction)
{
    const std::unique_ptr<ReactionEffectActor> owned = NS::CreateActor<ReactionEffectActor>();
    ReactionEffectActor& actor = *owned;
    NS::SubObject* keeper = actor.GetEffectKeeper();
    NS::SubObject* reaction = actor.HitReactionSubObj();
    ASSERT_NE(keeper, nullptr);
    ASSERT_NE(reaction, nullptr);

    std::vector<NS::SubObject*> parts;
    std::vector<std::string_view> keys;
    NS::ForEachReflectedSubObj(actor, [&](const NS::SubObjectDesc& field, NS::SubObject* part) {
        parts.push_back(part);
        keys.push_back(field.key);
    });
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[1], reaction);
    EXPECT_EQ(parts[2], keeper);
    EXPECT_EQ(keys[2], "Effect");
}
