#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// 名前からクラスを引く表と、エディタで置ける印を縛る

namespace
{
    const NS::Obj::TypeRegistry::Entry* FindPlaceable(std::string_view className)
    {
        for (const NS::Obj::TypeRegistry::Entry* entry : NS::Obj::PlaceableEntries())
        {
            if (className == entry->className)
                return entry;
        }
        return nullptr;
    }
} // namespace

TEST(ActorRegistry, PlaceableClassesHaveLabels)
{
    // 置ける Actor はどれも表示名を持つ
    const std::vector<std::string_view> expected{"MapParts", "MapObj", "Goal", "KillZone", "FollowCamera", "Light"};
    for (const std::string_view className : expected)
    {
        const NS::Obj::TypeRegistry::Entry* entry = FindPlaceable(className);
        ASSERT_NE(entry, nullptr) << className;
        ASSERT_NE(entry->label, nullptr) << className;
        EXPECT_FALSE(std::string_view{entry->label}.empty()) << className;
        EXPECT_NE(entry->create, nullptr) << className;
    }
}

TEST(ActorRegistry, PlayerAndComponentsAreNotPlaceable)
{
    // プレイヤーは新しいレベルに自動で 1 体入るので置く一覧に出さない。部品も置く物ではない
    EXPECT_EQ(FindPlaceable("Player"), nullptr);
    EXPECT_EQ(FindPlaceable("MeshRenderer"), nullptr);
    EXPECT_EQ(FindPlaceable("GoalComponent"), nullptr);
}

TEST(ActorRegistry, PlaceableEntriesAreSortedByLabel)
{
    // 静的初期化の順に左右されず、メニューの並びが毎回同じになる
    const std::vector<const NS::Obj::TypeRegistry::Entry*>& entries = NS::Obj::PlaceableEntries();
    for (std::size_t i = 1; i < entries.size(); ++i)
    {
        EXPECT_LE(std::string_view{entries[i - 1]->label}, std::string_view{entries[i]->label});
    }
}

TEST(ActorRegistry, CreatedActorReportsItsClassName)
{
    // 生成した Actor の ClassName が登録名と一致しないと、保存して読み直した時に別の型になる
    for (const NS::Obj::TypeRegistry::Entry* entry : NS::Obj::PlaceableEntries())
    {
        const std::unique_ptr<NS::Obj::Actor> actor = entry->create();
        ASSERT_NE(actor, nullptr);
        EXPECT_EQ(std::string_view{actor->ClassName()}, std::string_view{entry->className});
    }
}

TEST(ActorRegistry, CreateRegisteredObjectUsesClass)
{
    nlohmann::json object = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(object, "MapObj");
    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::CreateRegisteredObject(object);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(std::string_view{actor->ClassName()}, "MapObj");
}

TEST(ActorRegistry, UnknownClassFallsBackToPlainActor)
{
    nlohmann::json object = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(object, "NoSuchActor");
    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::CreateRegisteredObject(object);
    ASSERT_NE(actor, nullptr);
    EXPECT_TRUE(std::string_view{actor->ClassName()}.empty());
}
