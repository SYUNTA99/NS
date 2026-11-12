#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DEffect.h"
#include "TestEffectActor.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include "TestSpawn.h"

// TestEffectActor.h の型の登録。effect_test.cpp も同じ型を使う
NS_CLASS(TestEffectActor)

// エフェクトの関数はインターフェースだけを受け、個体の部品 EffectObj へ渡す

TEST(IGetEffectTest, PlayEffectKeepsOneRecordInTheActorsPart)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    NS::Scene scene;
    TestEffectActor* spawned = SpawnFromActorFile<TestEffectActor>(scene, "TestEffectActor");
    ASSERT_NE(spawned, nullptr);
    TestEffectActor& actor = *spawned;
    ASSERT_NE(actor.GetEffect(), nullptr);

    const NS::uint32 id = NS::PlayEffect(&actor, &asset, NS::Gfx::EffectPlayDesc{});

    EXPECT_NE(id, 0u);
    ASSERT_EQ(actor.GetEffect()->Records().size(), 1u);
    EXPECT_EQ(actor.GetEffect()->Records()[0].id, id);
    EXPECT_EQ(actor.GetEffect()->Records()[0].asset, &asset);
}

TEST(IGetEffectTest, StopEffectAndStopEffectRootForgetTheRecord)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    NS::Scene scene;
    TestEffectActor* spawned = SpawnFromActorFile<TestEffectActor>(scene, "TestEffectActor");
    ASSERT_NE(spawned, nullptr);
    TestEffectActor& actor = *spawned;
    const NS::uint32 stopped = NS::PlayEffect(&actor, &asset, NS::Gfx::EffectPlayDesc{});
    const NS::uint32 rootStopped = NS::PlayFollowingEffect(&actor, &asset, NS::Gfx::EffectPlayDesc{});
    ASSERT_EQ(actor.GetEffect()->Records().size(), 2u);

    NS::StopEffect(&actor, stopped);
    ASSERT_EQ(actor.GetEffect()->Records().size(), 1u);
    EXPECT_EQ(actor.GetEffect()->Records()[0].id, rootStopped);
    NS::StopEffectRoot(&actor, rootStopped);

    EXPECT_TRUE(actor.GetEffect()->Records().empty());
}

TEST(IGetEffectTest, ActorWithoutThePartDoesNothing)
{
    NS::Effect asset{"Assets/Effects/test.spark.efkefc", nullptr};
    NS::SceneActor actor;
    ASSERT_EQ(actor.GetEffect(), nullptr);

    EXPECT_EQ(NS::PlayEffect(&actor, &asset, NS::Gfx::EffectPlayDesc{}), 0u);
    EXPECT_EQ(NS::PlayFollowingEffect(&actor, &asset, NS::Gfx::EffectPlayDesc{}), 0u);
    NS::StopEffect(&actor, 1);
    NS::StopEffectRoot(&actor, 1);
    NS::SetEffectTransform(&actor, 1, NS::Vector3{}, NS::Quaternion::Identity, NS::Vector3{1.0f, 1.0f, 1.0f});
    NS::SetEffectDynamicInput(&actor, 1, 0, 1.0f);
}
