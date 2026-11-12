#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DEffect.h"
#include "TestEffectActor.h"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "NSlib/Core/Types.h"
#include "TestSpawn.h"

// 描画の無い世界の部品は再生せず、出した物を止めるまで一覧に残す

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

    using NS::EffectObj;
    using NS::EffectRecord;
} // namespace

TEST(EffectTest, PlayWithoutAnEffectSceneKeepsOneRecord)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    EffectObj effect;
    const NS::uint32 id = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});

    EXPECT_NE(id, 0u);
    ASSERT_EQ(effect.Records().size(), 1u);
    const EffectRecord& record = effect.Records()[0];
    EXPECT_EQ(record.id, id);
    EXPECT_EQ(record.asset, &asset);
    EXPECT_FALSE(record.follows);
    EXPECT_FALSE(record.handle.IsValid());
}

TEST(EffectTest, PlayFollowingMarksTheRecordAndKeepsTheScale)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    EffectObj effect;
    NS::Gfx::EffectPlayDesc desc;
    desc.scale = NS::Vector3{2.0f, 3.0f, 4.0f};
    const NS::uint32 id = effect.PlayFollowing(nullptr, &asset, desc);

    EXPECT_NE(id, 0u);
    ASSERT_EQ(effect.Records().size(), 1u);
    EXPECT_TRUE(effect.Records()[0].follows);
    EXPECT_EQ(effect.Records()[0].scale, desc.scale);
}

TEST(EffectTest, EmptyAssetIsNotPlayed)
{
    NS::Effect empty{"", nullptr};
    EffectObj effect;

    EXPECT_EQ(effect.Play(nullptr, nullptr, NS::Gfx::EffectPlayDesc{}), 0u);
    EXPECT_EQ(effect.Play(nullptr, &empty, NS::Gfx::EffectPlayDesc{}), 0u);
    EXPECT_TRUE(effect.Records().empty());
}

TEST(EffectTest, StopAndStopRootForgetOnlyTheirRecord)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    EffectObj effect;
    const NS::uint32 first = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    const NS::uint32 second = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    const NS::uint32 third = effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    ASSERT_NE(first, second);

    effect.StopRoot(nullptr, first);
    effect.Stop(nullptr, third);
    // 無い番号は何もしない
    effect.Stop(nullptr, first);

    ASSERT_EQ(effect.Records().size(), 1u);
    EXPECT_EQ(effect.Records()[0].id, second);
}

TEST(EffectTest, StopRootAllForgetsEveryRecord)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    EffectObj effect;
    (void)effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    (void)effect.PlayFollowing(nullptr, &asset, NS::Gfx::EffectPlayDesc{});

    effect.StopRootAll(nullptr);

    EXPECT_TRUE(effect.Records().empty());
}

// 描画の無い世界では終わったかを問えないので、出すだけの再生は次の Update で外し、追従する再生は残す
TEST(EffectTest, UpdateWithoutRenderingForgetsOnlyTheRecordsThatDoNotFollow)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    EffectObj effect;
    (void)effect.Play(nullptr, &asset, NS::Gfx::EffectPlayDesc{});
    const NS::uint32 following = effect.PlayFollowing(nullptr, &asset, NS::Gfx::EffectPlayDesc{});

    effect.Update(nullptr, NS::Vector3{}, NS::Quaternion::Identity);

    ASSERT_EQ(effect.Records().size(), 1u);
    EXPECT_EQ(effect.Records()[0].id, following);
}

// 世界から外れるアクターは、出していたエフェクトを基底が全部止める
TEST(EffectTest, DisappearingActorStopsEveryEffectItPlayed)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    NS::Scene scene;
    TestEffectActor* spawned = SpawnFromActorFile<TestEffectActor>(scene, "TestEffectActor");
    ASSERT_NE(spawned, nullptr);
    TestEffectActor& actor = *spawned;
    ASSERT_NE(NS::PlayEffect(&actor, &asset, NS::Gfx::EffectPlayDesc{}), 0u);
    ASSERT_NE(NS::PlayFollowingEffect(&actor, &asset, NS::Gfx::EffectPlayDesc{}), 0u);
    ASSERT_EQ(actor.GetEffect()->Records().size(), 2u);

    actor.Disappear();

    EXPECT_TRUE(actor.GetEffect()->Records().empty());
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
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[1], reaction);
    EXPECT_EQ(parts[2], effect);
    EXPECT_EQ(keys[2], "Effect");
}
