#include "Game/Level/KillZone.h"
#include "Game/Player.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Filesystem.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

// シーンの保存と読込が Actor のクラスを保つことを縛る
// 部品の構成はクラスが決め、データは値だけを運ぶ

namespace
{
    // class と id だけを持つ配置物を doc へ足す
    void AddClassObject(nlohmann::json& doc, std::string_view className, std::uint32_t id)
    {
        nlohmann::json object = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(object, className);
        NS::Obj::SetObjectJsonId(object, id);
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(object));
    }
} // namespace

TEST(SceneActor, ClassOnlyObjectBuildsWholeComposition)
{
    // 部品の一覧を持たないデータでも、クラスのコンストラクタが部品を全て積む
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    AddClassObject(doc, "MapObj", 1);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);

    NS::Obj::Actor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(std::string_view{actor->ClassName()}, "MapObj");
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::SphereCollider>(actor->Part("Collision")), nullptr);
}

TEST(SceneActor, ToJsonKeepsClassAndValues)
{
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    AddClassObject(doc, "MapObj", 1);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    NS::Obj::Actor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    NS::Obj::SphereCollider* sphere = NS::Obj::ComponentCast<NS::Obj::SphereCollider>(actor->Part("Collision"));
    ASSERT_NE(sphere, nullptr);
    sphere->SetRadius(1.5f);
    actor->Root().SetPosition(NS::Core::Vector3{3.0f, 4.0f, 5.0f});

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_EQ(NS::Obj::ObjectJsonClass(objects[0]), "MapObj");

    // 読み直すと同じクラスで組まれ、変えた値が戻る
    NS::Obj::Scene reloaded;
    reloaded.LoadJson(saved);
    NS::Obj::Actor* again = reloaded.Objects().FindByObjectId(1);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(std::string_view{again->ClassName()}, "MapObj");
    const NS::Obj::SphereCollider* sphereAgain =
        NS::Obj::ComponentCast<NS::Obj::SphereCollider>(again->Part("Collision"));
    ASSERT_NE(sphereAgain, nullptr);
    EXPECT_FLOAT_EQ(sphereAgain->Radius(), 1.5f);
    EXPECT_FLOAT_EQ(again->Root().Position().x, 3.0f);
    EXPECT_FLOAT_EQ(again->Root().Position().y, 4.0f);
    EXPECT_FLOAT_EQ(again->Root().Position().z, 5.0f);
}

TEST(SceneActor, FileTextRoundTripKeepsClass)
{
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    AddClassObject(doc, "MapParts", 1);
    AddClassObject(doc, "Goal", 2);

    const std::string text = NS::Obj::SerializeSceneToJson(doc);
    nlohmann::json back;
    ASSERT_TRUE(NS::Obj::DeserializeSceneFromJson(back, text));

    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(back);
    ASSERT_EQ(objects.size(), 2u);
    EXPECT_EQ(NS::Obj::ObjectJsonClass(objects[NS::Obj::FindObjectIndexById(back, 1)]), "MapParts");
    EXPECT_EQ(NS::Obj::ObjectJsonClass(objects[NS::Obj::FindObjectIndexById(back, 2)]), "Goal");
}

TEST(SceneActor, GravityDirectionRoundTripsThroughFileAndScene)
{
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    NS::Obj::SetSceneJsonGravityDirection(doc, NS::Core::Vector3{3.0f, 0.0f, 0.0f});

    const std::string text = NS::Obj::SerializeSceneToJson(doc);
    nlohmann::json decoded;
    ASSERT_TRUE(NS::Obj::DeserializeSceneFromJson(decoded, text));
    EXPECT_EQ(NS::Obj::SceneJsonGravityDirection(decoded), (NS::Core::Vector3{1.0f, 0.0f, 0.0f}));

    NS::Obj::Scene scene;
    scene.LoadJson(decoded);
    EXPECT_EQ(scene.GravityDirection(), (NS::Core::Vector3{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(scene.Physics().Gravity(), (NS::Core::Vector3{25.0f, 0.0f, 0.0f}));
    EXPECT_EQ(NS::Obj::SceneJsonGravityDirection(scene.ToJson()), (NS::Core::Vector3{1.0f, 0.0f, 0.0f}));
}

TEST(SceneActor, InvalidGravityDirectionFallsBackToDown)
{
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    doc["environment"]["gravityDirection"] = nlohmann::json::array({0.0f, 0.0f, 0.0f});
    EXPECT_EQ(NS::Obj::SceneJsonGravityDirection(doc), (NS::Core::Vector3{0.0f, -1.0f, 0.0f}));

    NS::Obj::Scene scene;
    scene.SetGravityDirection(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    EXPECT_EQ(scene.GravityDirection(), (NS::Core::Vector3{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ(scene.Physics().Gravity(), (NS::Core::Vector3{0.0f, -25.0f, 0.0f}));
}

TEST(SceneActor, PhysicsSettingsIsNotAPlaceableComponent)
{
    EXPECT_EQ(NS::Obj::TypeRegistry::Get().Find("PhysicsSettings"), nullptr);
}

TEST(SceneActor, ShippedSceneUsesRegisteredClasses)
{
    // 出荷シーンの配置物はどれも登録済みのクラスを持つ。素の Actor で組まれる物が無い
    const std::string path = NS::Platform::FileSystem::Combine(
        NS::Platform::FileSystem::Combine(
            NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Assets"), "Scenes"),
        "new_scene.scene");
    nlohmann::json doc;
    ASSERT_TRUE(NS::Obj::LoadSceneFromJsonFile(doc, path));

    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(doc);
    ASSERT_FALSE(objects.empty());
    for (const nlohmann::json& object : objects)
    {
        const std::string_view className = NS::Obj::ObjectJsonClass(object);
        ASSERT_FALSE(className.empty()) << NS::Obj::ObjectJsonName(object);
        const NS::Obj::TypeRegistry::Entry* entry = NS::Obj::TypeRegistry::Get().Find(className);
        ASSERT_NE(entry, nullptr) << className;
        EXPECT_NE(entry->create, nullptr) << className;
    }
}

TEST(SceneActor, NewLevelGetsOnePlayerAndOneKillZone)
{
    // 新しいレベルにはプレイヤーと落下死の範囲が 1 つずつ入り、2 度呼んでも増えない
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    EXPECT_TRUE(EnsurePlayerObject(doc));
    EXPECT_TRUE(NS::Game::Level::EnsureKillZoneObject(doc));
    EXPECT_FALSE(EnsurePlayerObject(doc));
    EXPECT_FALSE(NS::Game::Level::EnsureKillZoneObject(doc));

    int players = 0;
    int zones = 0;
    for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(doc))
    {
        if (NS::Obj::ObjectJsonClass(object) == "Player")
        {
            ++players;
        }
        if (NS::Obj::ObjectJsonClass(object) == "KillZone")
        {
            ++zones;
        }
        EXPECT_NE(NS::Obj::ObjectJsonId(object), NS::Obj::k_NoObjectId);
    }
    EXPECT_EQ(players, 1);
    EXPECT_EQ(zones, 1);
}
