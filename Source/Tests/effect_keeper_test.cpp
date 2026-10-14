#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/EffectKeeper.h"
#include "NSlib/Object/SubObjects/HitReaction.h"
#include "TestEffectActor.h"

#include <gtest/gtest.h>

#include <vector>

#include <cstdint>

// 描画の無い世界の保持役は再生せず、出した記録だけを残す

namespace
{
    class ReactionEffectActor final : public TestEffectActor
    {
    protected:
        void OnInit() override
        {
            TestEffectActor::OnInit();
            (void)CreateSubObj(m_hitReaction, "HitReaction");
        }
    };

    using NS::Obj::EffectKeeper;
    using NS::Obj::EffectRecord;
} // namespace

TEST(EffectKeeperTest, PlayWithoutAnOwnerLeavesOneRecord)
{
    EffectKeeper keeper;
    const std::uint32_t id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});

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
    EffectKeeper keeper;
    keeper.BeginStep();
    const std::uint32_t id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
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
    EffectKeeper keeper;
    const std::uint32_t id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
    keeper.StopAfter(id, 2);

    keeper.BeginStep();
    EXPECT_FALSE(keeper.Find(id)->endStep.has_value());
    keeper.BeginStep();
    ASSERT_TRUE(keeper.Find(id)->endStep.has_value());
    EXPECT_EQ(keeper.Find(id)->endStep.value(), 2);
}

TEST(EffectKeeperTest, EndedRecordIsDroppedAfterTheKeepSteps)
{
    EffectKeeper keeper{3};
    const std::uint32_t id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
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
    EffectKeeper keeper;
    const std::uint32_t id = keeper.Play("test.spark", NS::Gfx::EffectPlayDesc{});
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
    const std::unique_ptr<TestEffectActor> owned = NS::Obj::CreateActor<TestEffectActor>();
    TestEffectActor& actor = *owned;
    EffectKeeper* keeper = actor.GetEffectKeeper();
    ASSERT_NE(keeper, nullptr);
    ASSERT_EQ(keeper->Step(), 0);

    actor.Update();
    EXPECT_EQ(keeper->Step(), 1);

    keeper->SetActive(false);
    actor.Update();
    EXPECT_EQ(keeper->Step(), 1);
}

TEST(EffectKeeperTest, SlotKeeperIsOrderedAfterHitReaction)
{
    const std::unique_ptr<ReactionEffectActor> owned = NS::Obj::CreateActor<ReactionEffectActor>();
    ReactionEffectActor& actor = *owned;
    NS::Obj::SubObject* keeper = actor.GetEffectKeeper();
    NS::Obj::SubObject* reaction = actor.HitReactionSubObj();
    ASSERT_NE(keeper, nullptr);
    ASSERT_NE(reaction, nullptr);

    std::vector<NS::Obj::SubObject*> parts;
    actor.ForEachSubObj([&](NS::Obj::SubObject* part) { parts.push_back(part); });
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[1], reaction);
    EXPECT_EQ(parts[2], keeper);
    EXPECT_EQ(actor.FindSubObj("Effect"), keeper);
}
