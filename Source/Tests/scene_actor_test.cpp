#include "Editor/EditorObjects.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// シーンの保存と読込が SceneActor のクラスを保つことを縛る
// 部品の構成はクラスが決め、データは値だけを運ぶ

namespace
{
    // class と id だけを持つ配置物を doc へ足す
    void AddClassObject(nlohmann::json& doc, std::string_view className, NS::uint32 id)
    {
        nlohmann::json object = NS::MakeObjectJson();
        NS::SetObjectJsonClass(object, className);
        NS::SetObjectJsonId(object, id);
        NS::SceneJsonObjects(doc).push_back(std::move(object));
    }

    // 開始の手前で止まる ActorList の口を外から呼べるか。テンプレートにして、呼べない時を偽として受ける
    template <class List>
    concept CanAppendFromOutside =
        requires(List& list, std::unique_ptr<NS::SceneActor> obj) { list.Append(std::move(obj)); };
    template <class List>
    concept CanAppendWithNewIdFromOutside = requires(List& list, std::unique_ptr<NS::SceneActor> obj) {
        list.AppendWithNewId(std::move(obj), std::string{});
    };
    template <class List>
    concept CanInsertFromJsonFromOutside =
        requires(List& list, std::unique_ptr<NS::SceneActor> obj, const nlohmann::json& entry) {
            list.InsertFromJson(std::move(obj), entry, std::size_t{0});
        };
    template <class List>
    concept CanSpawnFromOutside = requires(List& list) { list.template Spawn<NS::SceneActor>(); };
} // namespace

TEST(SceneActor, SpawningGoesThroughTheSceneOnly)
{
    // 配置物を湧かすのは開始まで済ませる Scene の口だけ。開始を飛ばして並びへ入れる道を外へ開けない
    static_assert(!CanAppendFromOutside<NS::ActorList>);
    static_assert(!CanAppendWithNewIdFromOutside<NS::ActorList>);
    static_assert(!CanInsertFromJsonFromOutside<NS::ActorList>);
    static_assert(!CanSpawnFromOutside<NS::ActorList>);
}

TEST(SceneActor, ClassOnlyObjectBuildsWholeComposition)
{
    // 部品の一覧を持たないデータでも、クラスの OnInit が部品を全て積む
    nlohmann::json doc = NS::MakeSceneJson();
    AddClassObject(doc, "PropActor", 1);

    NS::Scene scene;
    scene.ResetFromJson(doc);

    NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(std::string_view{actor->ClassName()}, "PropActor");
    EXPECT_NE(NS::Cast<NS::SphereCollision>(actor->CollisionClassObj()), nullptr);
}

TEST(SceneActor, ToJsonKeepsClassAndValues)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddClassObject(doc, "PropActor", 1);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    sphere->SetRadius(1.5f);
    actor->Root().SetPosition(NS::Vector3{3.0f, 4.0f, 5.0f});

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_EQ(NS::ObjectJsonClass(objects[0]), "PropActor");

    // 読み直すと同じクラスで組まれ、変えた値が戻る
    NS::Scene reloaded;
    reloaded.ResetFromJson(saved);
    NS::SceneActor* again = reloaded.Objects().FindByObjectId(1);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(std::string_view{again->ClassName()}, "PropActor");
    const NS::SphereCollision* sphereAgain = NS::Cast<NS::SphereCollision>(again->CollisionClassObj());
    ASSERT_NE(sphereAgain, nullptr);
    EXPECT_FLOAT_EQ(sphereAgain->Radius(), 1.5f);
    EXPECT_FLOAT_EQ(again->Root().Position().x, 3.0f);
    EXPECT_FLOAT_EQ(again->Root().Position().y, 4.0f);
    EXPECT_FLOAT_EQ(again->Root().Position().z, 5.0f);
}

TEST(SceneActor, UnknownClassObjKeyIsWarnedAndOtherClassObjsAreRead)
{
    // クラスの表に無い鍵は警告して読み飛ばし、表にある部品の値は読む
    nlohmann::json doc = NS::MakeSceneJson();
    AddClassObject(doc, "PropActor", 1);
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    sphere->SetRadius(1.5f);

    nlohmann::json saved = scene.ToJson();
    nlohmann::json& objects = NS::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    NS::ObjectJsonClassObjs(objects[0])["NotInTheClass"] = nlohmann::json{{"Radius", 9.0f}};

    const TestLogCapture log;
    NS::Scene reloaded;
    reloaded.ResetFromJson(saved);
    EXPECT_EQ(log.WarningCount(), 1);
    const NS::SceneActor* again = reloaded.Objects().FindByObjectId(1);
    ASSERT_NE(again, nullptr);
    const NS::SphereCollision* sphereAgain = NS::Cast<NS::SphereCollision>(again->CollisionClassObj());
    ASSERT_NE(sphereAgain, nullptr);
    EXPECT_FLOAT_EQ(sphereAgain->Radius(), 1.5f);
}

TEST(SceneActor, FileTextRoundTripKeepsClass)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddClassObject(doc, "MapPartsActor", 1);
    AddClassObject(doc, "GoalActor", 2);

    const std::string text = NS::SerializeSceneToJson(doc);
    nlohmann::json back;
    ASSERT_TRUE(NS::DeserializeSceneFromJson(back, text));

    const nlohmann::json& objects = NS::SceneJsonObjects(back);
    ASSERT_EQ(objects.size(), 2u);
    EXPECT_EQ(NS::ObjectJsonClass(objects[NS::FindObjectIndexById(back, 1)]), "MapPartsActor");
    EXPECT_EQ(NS::ObjectJsonClass(objects[NS::FindObjectIndexById(back, 2)]), "GoalActor");
}

TEST(SceneActor, EnvironmentRoundTripsThroughFileAndScene)
{
    nlohmann::json doc = NS::MakeSceneJson();
    NS::WriteSceneEnvironment(
        doc, NS::SceneEnvironment{.skybox = "Assets/Skybox/kurt", .gravityDirection = NS::Vector3{3.0f, 0.0f, 0.0f}});

    const std::string text = NS::SerializeSceneToJson(doc);
    nlohmann::json decoded;
    ASSERT_TRUE(NS::DeserializeSceneFromJson(decoded, text));
    const NS::SceneEnvironment read = NS::ReadSceneEnvironment(decoded);
    EXPECT_EQ(read.skybox, "Assets/Skybox/kurt");
    EXPECT_EQ(read.gravityDirection, (NS::Vector3{1.0f, 0.0f, 0.0f}));

    NS::Scene scene;
    scene.ResetFromJson(decoded);
    EXPECT_EQ(scene.SkyboxPath(), "Assets/Skybox/kurt");
    EXPECT_EQ(scene.GravityDirection(), (NS::Vector3{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(scene.Physics().Gravity(), (NS::Vector3{25.0f, 0.0f, 0.0f}));
    const NS::SceneEnvironment saved = NS::ReadSceneEnvironment(scene.ToJson());
    EXPECT_EQ(saved.skybox, "Assets/Skybox/kurt");
    EXPECT_EQ(saved.gravityDirection, (NS::Vector3{1.0f, 0.0f, 0.0f}));
}

TEST(SceneActor, InvalidGravityDirectionFallsBackToDown)
{
    nlohmann::json doc = NS::MakeSceneJson();
    doc["environment"]["gravityDirection"] = nlohmann::json::array({0.0f, 0.0f, 0.0f});
    EXPECT_EQ(NS::ReadSceneEnvironment(doc).gravityDirection, (NS::Vector3{0.0f, -1.0f, 0.0f}));

    NS::Scene scene;
    scene.SetGravityDirection(NS::Vector3{0.0f, 0.0f, 0.0f});
    EXPECT_EQ(scene.GravityDirection(), (NS::Vector3{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ(scene.Physics().Gravity(), (NS::Vector3{0.0f, -25.0f, 0.0f}));
}

TEST(SceneLoad, EveryShippedSceneWritesBackByteForByte)
{
    // 読んで書き戻すとファイルと同じ文字になる。形をまとめ直しても保存の形は変えない
    const std::filesystem::path folder = std::filesystem::path{NS::OS::FileSystem::ContentRoot()} / "Assets" / "Scenes";
    int checked = 0;
    for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator{folder})
    {
        if (file.path().extension() != ".scene")
        {
            continue;
        }
        const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(file.path().string());
        ASSERT_TRUE(text.has_value()) << file.path().string();
        nlohmann::json doc;
        ASSERT_TRUE(NS::DeserializeSceneFromJson(doc, *text)) << file.path().string();
        EXPECT_EQ(NS::SerializeSceneToJson(doc), *text) << file.path().string();
        ++checked;
    }
    EXPECT_GT(checked, 0);
}

TEST(SceneJson, RefValueRoundTripsAndRejectsOtherShapes)
{
    // 参照の欄は {"ref": id}。名前で書かれた参照と壊れた形は id として読まない
    const nlohmann::json value = NS::MakeRefValue(NS::ActorRef{7});
    EXPECT_TRUE(NS::IsRefValue(value));
    NS::ActorRef read{};
    ASSERT_TRUE(NS::ReadRefValue(value, read));
    EXPECT_EQ(read.id, 7u);

    NS::ActorRef untouched{3};
    EXPECT_FALSE(NS::ReadRefValue(nlohmann::json{{"ref", "Goal"}}, untouched));
    EXPECT_FALSE(NS::ReadRefValue(nlohmann::json{{"ref", -1}}, untouched));
    EXPECT_FALSE(NS::ReadRefValue(nlohmann::json(7), untouched));
    EXPECT_EQ(untouched.id, 3u);
}

TEST(SceneJson, ObjectHeaderRoundTrips)
{
    nlohmann::json object = NS::MakeObjectJson();
    NS::WriteObjectHeader(object, NS::ObjectHeader{.id = 4, .className = "PropActor", .name = "Ball", .active = false});
    const NS::ObjectHeader header = NS::ReadObjectHeader(object);
    EXPECT_EQ(header.id, 4u);
    EXPECT_EQ(header.className, "PropActor");
    EXPECT_EQ(header.name, "Ball");
    EXPECT_FALSE(header.active);
}

TEST(SceneJson, NextFreeObjectIdStepsPastTheLargestId)
{
    // 手編集で nextObjectId が配置物の id より小さいファイルもある。次の id は最大の id の次より前に戻らない
    nlohmann::json doc = NS::MakeSceneJson();
    EXPECT_EQ(NS::NextFreeObjectId(doc), 1u);
    AddClassObject(doc, "PropActor", 5);
    NS::SetSceneJsonNextObjectId(doc, 2);
    EXPECT_EQ(NS::NextFreeObjectId(doc), 6u);
    NS::SetSceneJsonNextObjectId(doc, 10);
    EXPECT_EQ(NS::NextFreeObjectId(doc), 10u);
}

TEST(SceneActor, ApplyFromJsonNumbersANameThatIsAlreadyUsed)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddClassObject(doc, "PropActor", 1);
    AddClassObject(doc, "PropActor", 2);
    NS::SetObjectJsonName(NS::SceneJsonObjects(doc)[0], "Ball");
    NS::SetObjectJsonName(NS::SceneJsonObjects(doc)[1], "Other");
    NS::Scene scene;
    scene.ResetFromJson(doc);

    nlohmann::json snapshot = NS::SceneJsonObjects(scene.ToJson())[1];
    NS::SetObjectJsonName(snapshot, "Ball");
    const NS::SceneActor* applied = scene.ApplyFromJson(snapshot);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->Name(), "Ball_1");
}

TEST(SceneActor, PhysicsSettingsIsNotAPlaceableClassObj)
{
    EXPECT_EQ(NS::TypeRegistry::Get().Find("PhysicsSettings"), nullptr);
}

TEST(SceneActor, ShippedSceneUsesRegisteredClasses)
{
    // 出荷シーンの配置物はどれも登録済みのクラスを持つ。素の SceneActor で組まれる物が無い
    const std::string path = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Scenes"),
        "new_scene.scene");
    nlohmann::json doc;
    ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, path));

    const nlohmann::json& objects = NS::SceneJsonObjects(doc);
    ASSERT_FALSE(objects.empty());
    for (const nlohmann::json& object : objects)
    {
        const std::string_view className = NS::ObjectJsonClass(object);
        ASSERT_FALSE(className.empty()) << NS::ObjectJsonName(object);
        const NS::TypeRegistry::Entry* entry = NS::TypeRegistry::Get().Find(className);
        ASSERT_NE(entry, nullptr) << className;
        EXPECT_NE(entry->create, nullptr) << className;
    }
}

TEST(SceneActor, NewLevelGetsOnePlayerAndOneDeathZone)
{
    // 新しいレベルにはプレイヤーと落下死の範囲が 1 つずつ入り、2 度呼んでも増えない
    nlohmann::json doc = NS::MakeSceneJson();
    EXPECT_TRUE(NS::Editor::EnsurePlayerObject(doc));
    EXPECT_TRUE(NS::Editor::EnsureDeathZoneObject(doc));
    EXPECT_FALSE(NS::Editor::EnsurePlayerObject(doc));
    EXPECT_FALSE(NS::Editor::EnsureDeathZoneObject(doc));

    int players = 0;
    int zones = 0;
    for (const nlohmann::json& object : NS::SceneJsonObjects(doc))
    {
        if (NS::ObjectJsonClass(object) == "PlayerActor")
        {
            ++players;
        }
        if (NS::ObjectJsonClass(object) == "DeathZoneActor")
        {
            ++zones;
        }
        EXPECT_NE(NS::ObjectJsonId(object), NS::k_NoObjectId);
    }
    EXPECT_EQ(players, 1);
    EXPECT_EQ(zones, 1);
}

namespace
{
    // PlayerActor の種類の既定を試しの間だけ差し替え、終わったら元へ戻す
    class ScopedPlayerArchetype
    {
    public:
        explicit ScopedPlayerArchetype(nlohmann::json archetype)
        {
            const nlohmann::json* current = NS::ArchetypeLibrary::Get().Find("PlayerActor");
            if (current != nullptr)
            {
                m_previous = *current;
            }
            NS::ArchetypeLibrary::Get().Set("PlayerActor", std::move(archetype));
        }

        ~ScopedPlayerArchetype()
        {
            if (m_previous.has_value())
            {
                NS::ArchetypeLibrary::Get().Set("PlayerActor", std::move(*m_previous));
            }
            else
            {
                NS::ArchetypeLibrary::Get().Erase("PlayerActor");
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
    const ScopedPlayerArchetype archetype{nlohmann::json{
        {"class", "PlayerActor"}, {"classObjs", {{"Collider", {{"半径", 0.65f}, {"半分の高さ", 0.5f}}}}}}};
    nlohmann::json doc = NS::MakeSceneJson();
    EXPECT_TRUE(NS::Editor::EnsurePlayerObject(doc));

    const nlohmann::json& objects = NS::SceneJsonObjects(doc);
    ASSERT_EQ(objects.size(), std::size_t{1});
    EXPECT_NEAR(NS::ObjectPosition(objects[0]).y, 0.5f + 0.5f + 0.65f + 0.01f, 1e-5f);
}

TEST(SceneActor, EnsuredPlayerAndRestartWithoutBaselineShareTheDefaultSpawnPosition)
{
    // 補う位置とやり直しの落ち先は同じ DefaultSpawnPosition から出る。数字の写しが戻ると片方だけずれる
    const ScopedPlayerArchetype archetype{nlohmann::json{
        {"class", "PlayerActor"}, {"classObjs", {{"Collider", {{"半径", 0.65f}, {"半分の高さ", 0.5f}}}}}}};
    nlohmann::json doc = NS::MakeSceneJson();
    ASSERT_TRUE(NS::Editor::EnsurePlayerObject(doc));
    const NS::Vector3 ensured = NS::ObjectPosition(NS::SceneJsonObjects(doc)[0]);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 fallback = player->DefaultSpawnPosition();
    EXPECT_FLOAT_EQ(ensured.y, fallback.y);

    // 凍結に自機が居ないやり直しは、補う位置と同じ高さへ戻る
    player->Root().SetPosition(NS::Vector3{3.0f, 40.0f, 3.0f});
    player->RestartFrom(NS::MakeSceneJson());
    EXPECT_FLOAT_EQ(player->Root().Position().x, fallback.x);
    EXPECT_FLOAT_EQ(player->Root().Position().y, fallback.y);
    EXPECT_FLOAT_EQ(player->Root().Position().z, fallback.z);
}

TEST(SceneLoad, RejectsPreviousHierarchySchema)
{
    nlohmann::json previous = NS::MakeSceneJson();
    previous["version"] = 6;
    nlohmann::json loaded;
    const TestLogCapture log;
    EXPECT_FALSE(NS::DeserializeSceneFromJson(loaded, previous.dump()));
    EXPECT_EQ(log.ErrorCount(), 1);
}

TEST(SceneLoad, RejectsVersionBeforeTheClassObjsKey)
{
    // 版 7 のファイルは持ち物を別の鍵に書いていた。読むと全部の値が黙って既定に戻る
    nlohmann::json previous = NS::MakeSceneJson();
    previous["version"] = 7;
    nlohmann::json loaded;
    const TestLogCapture log;
    EXPECT_FALSE(NS::DeserializeSceneFromJson(loaded, previous.dump()));
    EXPECT_EQ(log.ErrorCount(), 1);
}

TEST(SceneLoad, RegisteredClassBuildsItsTypeAndUnregisteredFallsBackToPlainActor)
{
    // クラス名は保存の鍵。登録の有るクラスはその型で組まれ、登録の無いクラスは素の SceneActor へ落ちる
    // クラスを改名して配置のデータを書き換え忘れると、落下の区域が素の SceneActor になり働かない
    nlohmann::json doc = NS::MakeSceneJson();
    AddClassObject(doc, "DeathZoneActor", 1);
    AddClassObject(doc, "UnregisteredActor", 2);
    NS::Scene scene;
    scene.ResetFromJson(doc);

    NS::SceneActor* zone = scene.Objects().FindByObjectId(1);
    ASSERT_NE(zone, nullptr);
    EXPECT_NE(NS::Cast<GL::Level::DeathZoneActor>(zone), nullptr);
    NS::SceneActor* plain = scene.Objects().FindByObjectId(2);
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(NS::Cast<GL::Level::DeathZoneActor>(plain), nullptr);
}

// 欄の表示名と部品の登録が保存の鍵なので、鍵が変わると読み直した値が静かに既定へ戻る
TEST(SceneLoad, EveryFieldKeySurvivesWritingBack)
{
    // 置けるクラスを 1 体ずつ、全欄を書いた形で置く。読んで書き戻し、全欄へ広げると、置いた鍵が全部残る
    NS::ArchetypeLibrary::Get().Reload();
    nlohmann::json doc = NS::MakeSceneJson();
    NS::uint32 id = 1;
    for (const NS::TypeRegistry::Entry* entry : NS::PlaceableEntries())
    {
        nlohmann::json object = NS::MakeObjectJson();
        NS::SetObjectJsonClass(object, entry->className);
        NS::SetObjectJsonId(object, id);
        ++id;
        NS::SceneJsonObjects(doc).push_back(NS::ExpandObjectJson(object));
    }
    ASSERT_FALSE(NS::SceneJsonObjects(doc).empty());
    NS::Scene scene;
    scene.ResetFromJson(doc);

    // 書き戻しは種類の既定と同じ欄を省くので、全欄へ広げてから、置いた鍵が全部残っているかを見る
    const nlohmann::json written = scene.ToJson();
    const nlohmann::json& writtenObjects = NS::SceneJsonObjects(written);
    const nlohmann::json& fileObjects = NS::SceneJsonObjects(doc);
    ASSERT_EQ(writtenObjects.size(), fileObjects.size());
    for (std::size_t i = 0; i < fileObjects.size(); ++i)
    {
        const nlohmann::json expanded = NS::ExpandObjectJson(writtenObjects[i]);
        const nlohmann::json& writtenParts = NS::ObjectJsonClassObjs(expanded);
        const nlohmann::json& fileParts = NS::ObjectJsonClassObjs(fileObjects[i]);
        for (nlohmann::json::const_iterator part = fileParts.begin(); part != fileParts.end(); ++part)
        {
            const std::string where = std::string{NS::ObjectJsonClass(fileObjects[i])} + " " + part.key();
            ASSERT_TRUE(writtenParts.contains(part.key())) << where;
            for (nlohmann::json::const_iterator field = part->begin(); field != part->end(); ++field)
            {
                EXPECT_TRUE(writtenParts[part.key()].contains(field.key())) << where << " " << field.key();
            }
        }
    }
}
