#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// 名前からクラスを引く表と、エディタで置ける印を縛る

namespace
{
    const NS::TypeRegistry::Entry* FindPlaceable(std::string_view className)
    {
        for (const NS::TypeRegistry::Entry* entry : NS::PlaceableEntries())
        {
            if (className == entry->className)
            {
                return entry;
            }
        }
        return nullptr;
    }
} // namespace

TEST(ActorRegistry, PlaceableClassesHaveLabels)
{
    // 置ける SceneActor はどれも表示名を持つ
    const std::vector<std::string_view> expected{
        "MapPartsActor", "PropActor", "GoalActor", "DeathZoneActor", "FollowCameraActor", "LightActor"};
    for (const std::string_view className : expected)
    {
        const NS::TypeRegistry::Entry* entry = FindPlaceable(className);
        ASSERT_NE(entry, nullptr) << className;
        ASSERT_NE(entry->label, nullptr) << className;
        EXPECT_FALSE(std::string_view{entry->label}.empty()) << className;
        EXPECT_NE(entry->create, nullptr) << className;
    }
}

TEST(ActorRegistry, PlayerAndClassObjsAreNotPlaceable)
{
    // プレイヤーは新しいレベルに自動で 1 体入るので置く一覧に出さない。部品も置く物ではない
    EXPECT_EQ(FindPlaceable("PlayerActor"), nullptr);
    EXPECT_EQ(FindPlaceable("Model"), nullptr);
    EXPECT_EQ(FindPlaceable("GoalParams"), nullptr);
}

TEST(ActorRegistry, PlaceableEntriesAreSortedByLabel)
{
    // 静的初期化の順に左右されず、メニューの並びが毎回同じになる
    const std::vector<const NS::TypeRegistry::Entry*>& entries = NS::PlaceableEntries();
    for (std::size_t i = 1; i < entries.size(); ++i)
    {
        EXPECT_LE(std::string_view{entries[i - 1]->label}, std::string_view{entries[i]->label});
    }
}

TEST(ActorRegistry, CreatedActorReportsItsClassName)
{
    // 生成した SceneActor の ClassName が登録名と一致しないと、保存して読み直した時に別の型になる
    for (const NS::TypeRegistry::Entry* entry : NS::PlaceableEntries())
    {
        const std::unique_ptr<NS::SceneActor> actor = entry->create();
        ASSERT_NE(actor, nullptr);
        EXPECT_EQ(std::string_view{actor->ClassName()}, std::string_view{entry->className});
    }
}

TEST(ActorRegistry, CreateRegisteredObjectUsesClass)
{
    nlohmann::json object = NS::MakeObjectJson();
    NS::SetObjectJsonClass(object, "PropActor");
    const std::unique_ptr<NS::SceneActor> actor = NS::CreateRegisteredObject(object);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(std::string_view{actor->ClassName()}, "PropActor");
}

TEST(ActorRegistry, UnknownClassFallsBackToPlainActor)
{
    nlohmann::json object = NS::MakeObjectJson();
    NS::SetObjectJsonClass(object, "NoSuchActor");
    const std::unique_ptr<NS::SceneActor> actor = NS::CreateRegisteredObject(object);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(std::string_view{actor->ClassName()}, "SceneActor");
}

TEST(ActorRegistry, RetiredMapObjectAbilitiesCannotBeCreated)
{
    EXPECT_EQ(NS::TypeRegistry::Get().Find("LaunchedBody"), nullptr);
    EXPECT_EQ(NS::TypeRegistry::Get().Find("Breakable"), nullptr);
    EXPECT_EQ(NS::TypeRegistry::Get().Find("TackleReaction"), nullptr);
}
