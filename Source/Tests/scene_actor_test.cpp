#include "Editor/EditorObjects.h"
#include "Game/Level/DeathZone.h"
#include "Game/Player.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/SphereCollision.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Windows/Filesystem.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

    // 開始の手前で止まる ObjectList の口を外から呼べるか。テンプレートにして、呼べない時を偽として受ける
    template <class List>
    concept CanAppendFromOutside =
        requires(List& list, std::unique_ptr<NS::Obj::Actor> obj) { list.Append(std::move(obj)); };
    template <class List>
    concept CanAppendWithNewIdFromOutside = requires(List& list, std::unique_ptr<NS::Obj::Actor> obj) {
        list.AppendWithNewId(std::move(obj), std::string{});
    };
    template <class List>
    concept CanInsertFromJsonFromOutside =
        requires(List& list, std::unique_ptr<NS::Obj::Actor> obj, const nlohmann::json& entry) {
            list.InsertFromJson(std::move(obj), entry, std::size_t{0});
        };
    template <class List>
    concept CanSpawnFromOutside = requires(List& list) { list.template Spawn<NS::Obj::Actor>(); };
} // namespace

TEST(SceneActor, SpawningGoesThroughTheSceneOnly)
{
    // 配置物を湧かすのは開始まで済ませる Scene の口だけ。開始を飛ばして並びへ入れる道を外へ開けない
    static_assert(!CanAppendFromOutside<NS::Obj::ObjectList>);
    static_assert(!CanAppendWithNewIdFromOutside<NS::Obj::ObjectList>);
    static_assert(!CanInsertFromJsonFromOutside<NS::Obj::ObjectList>);
    static_assert(!CanSpawnFromOutside<NS::Obj::ObjectList>);
}

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
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::SphereCollision>(actor->Part("Collision")), nullptr);
}

TEST(SceneActor, ToJsonKeepsClassAndValues)
{
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    AddClassObject(doc, "MapObj", 1);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    NS::Obj::Actor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    NS::Obj::SphereCollision* sphere = NS::Obj::ComponentCast<NS::Obj::SphereCollision>(actor->Part("Collision"));
    ASSERT_NE(sphere, nullptr);
    sphere->SetRadius(1.5f);
    actor->Root().SetPosition(NS::Vector3{3.0f, 4.0f, 5.0f});

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
    const NS::Obj::SphereCollision* sphereAgain =
        NS::Obj::ComponentCast<NS::Obj::SphereCollision>(again->Part("Collision"));
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
    NS::Obj::SetSceneJsonGravityDirection(doc, NS::Vector3{3.0f, 0.0f, 0.0f});

    const std::string text = NS::Obj::SerializeSceneToJson(doc);
    nlohmann::json decoded;
    ASSERT_TRUE(NS::Obj::DeserializeSceneFromJson(decoded, text));
    EXPECT_EQ(NS::Obj::SceneJsonGravityDirection(decoded), (NS::Vector3{1.0f, 0.0f, 0.0f}));

    NS::Obj::Scene scene;
    scene.LoadJson(decoded);
    EXPECT_EQ(scene.GravityDirection(), (NS::Vector3{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(scene.Physics().Gravity(), (NS::Vector3{25.0f, 0.0f, 0.0f}));
    EXPECT_EQ(NS::Obj::SceneJsonGravityDirection(scene.ToJson()), (NS::Vector3{1.0f, 0.0f, 0.0f}));
}

TEST(SceneActor, InvalidGravityDirectionFallsBackToDown)
{
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    doc["environment"]["gravityDirection"] = nlohmann::json::array({0.0f, 0.0f, 0.0f});
    EXPECT_EQ(NS::Obj::SceneJsonGravityDirection(doc), (NS::Vector3{0.0f, -1.0f, 0.0f}));

    NS::Obj::Scene scene;
    scene.SetGravityDirection(NS::Vector3{0.0f, 0.0f, 0.0f});
    EXPECT_EQ(scene.GravityDirection(), (NS::Vector3{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ(scene.Physics().Gravity(), (NS::Vector3{0.0f, -25.0f, 0.0f}));
}

TEST(SceneActor, PhysicsSettingsIsNotAPlaceableComponent)
{
    EXPECT_EQ(NS::Obj::TypeRegistry::Get().Find("PhysicsSettings"), nullptr);
}

TEST(SceneActor, ShippedSceneUsesRegisteredClasses)
{
    // 出荷シーンの配置物はどれも登録済みのクラスを持つ。素の Actor で組まれる物が無い
    const std::string path = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Scenes"),
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

TEST(SceneActor, NewLevelGetsOnePlayerAndOneDeathZone)
{
    // 新しいレベルにはプレイヤーと落下死の範囲が 1 つずつ入り、2 度呼んでも増えない
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    EXPECT_TRUE(NS::Editor::EnsurePlayerObject(doc));
    EXPECT_TRUE(NS::Editor::EnsureDeathZoneObject(doc));
    EXPECT_FALSE(NS::Editor::EnsurePlayerObject(doc));
    EXPECT_FALSE(NS::Editor::EnsureDeathZoneObject(doc));

    int players = 0;
    int zones = 0;
    for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(doc))
    {
        if (NS::Obj::ObjectJsonClass(object) == "Player")
        {
            ++players;
        }
        if (NS::Obj::ObjectJsonClass(object) == "DeathZone")
        {
            ++zones;
        }
        EXPECT_NE(NS::Obj::ObjectJsonId(object), NS::Obj::k_NoObjectId);
    }
    EXPECT_EQ(players, 1);
    EXPECT_EQ(zones, 1);
}

namespace
{
    // Player の種類の既定を試しの間だけ差し替え、終わったら元へ戻す
    class ScopedPlayerArchetype
    {
    public:
        explicit ScopedPlayerArchetype(nlohmann::json archetype)
        {
            const nlohmann::json* current = NS::Obj::ArchetypeLibrary::Get().Find("Player");
            if (current != nullptr)
            {
                m_previous = *current;
            }
            NS::Obj::ArchetypeLibrary::Get().Set("Player", std::move(archetype));
        }

        ~ScopedPlayerArchetype()
        {
            if (m_previous.has_value())
            {
                NS::Obj::ArchetypeLibrary::Get().Set("Player", std::move(*m_previous));
            }
            else
            {
                NS::Obj::ArchetypeLibrary::Get().Erase("Player");
            }
        }

        ScopedPlayerArchetype(const ScopedPlayerArchetype&) = delete;
        ScopedPlayerArchetype& operator=(const ScopedPlayerArchetype&) = delete;

    private:
        std::optional<nlohmann::json> m_previous;
    };
} // namespace

TEST(SceneActor, EnsuredPlayerStandsOnTheAssumedFloorForItsCapsule)
{
    // 補う自機の高さはカプセルの寸法から出す。床の上面 0.5 + 半分の高さ + 半径 + 余白 1cm
    // 種類の既定の半径を変えても、足元が床の 1cm 上に出る
    const ScopedPlayerArchetype archetype{
        nlohmann::json{{"class", "Player"}, {"parts", {{"Collider", {{"半径", 0.65f}, {"半分の高さ", 0.5f}}}}}}};
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    EXPECT_TRUE(NS::Editor::EnsurePlayerObject(doc));

    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(doc);
    ASSERT_EQ(objects.size(), std::size_t{1});
    EXPECT_NEAR(NS::Obj::ObjectPosition(objects[0]).y, 0.5f + 0.5f + 0.65f + 0.01f, 1e-5f);
}

TEST(SceneActor, EnsuredPlayerAndRestartWithoutBaselineShareTheDefaultSpawnPosition)
{
    // 補う位置とやり直しの落ち先は同じ DefaultSpawnPosition から出る。数字の写しが戻ると片方だけずれる
    const ScopedPlayerArchetype archetype{
        nlohmann::json{{"class", "Player"}, {"parts", {{"Collider", {{"半径", 0.65f}, {"半分の高さ", 0.5f}}}}}}};
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    ASSERT_TRUE(NS::Editor::EnsurePlayerObject(doc));
    const NS::Vector3 ensured = NS::Obj::ObjectPosition(NS::Obj::SceneJsonObjects(doc)[0]);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 fallback = player->DefaultSpawnPosition();
    EXPECT_FLOAT_EQ(ensured.y, fallback.y);

    // 凍結に自機が居ないやり直しは、補う位置と同じ高さへ戻る
    player->Root().SetPosition(NS::Vector3{3.0f, 40.0f, 3.0f});
    player->RestartFrom(NS::Obj::MakeSceneJson());
    EXPECT_FLOAT_EQ(player->Root().Position().x, fallback.x);
    EXPECT_FLOAT_EQ(player->Root().Position().y, fallback.y);
    EXPECT_FLOAT_EQ(player->Root().Position().z, fallback.z);
}

TEST(SceneLoad, CourseSceneHasOneDeathZoneAndNoUnregisteredActor)
{
    // 同梱の course.scene は落下死の範囲 (DeathZone) をちょうど 1 体持ち、未登録のクラスで素の Actor へ落ちる物が無い
    // クラス名は保存の鍵なので、クラスを改名してこのシーンを書き換え忘れるとここが赤になる
    const std::string path = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Scenes"),
        "course.scene");
    nlohmann::json doc;
    ASSERT_TRUE(NS::Obj::LoadSceneFromJsonFile(doc, path));

    int deathZones = 0;
    for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(doc))
    {
        const std::string_view className = NS::Obj::ObjectJsonClass(object);
        EXPECT_NE(NS::Obj::TypeRegistry::Get().Find(className), nullptr) << className;
        if (className == "DeathZone")
        {
            ++deathZones;
        }
    }
    EXPECT_EQ(deathZones, 1);
}
