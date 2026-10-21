#include "Editor/EditorObjects.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestActorFile.h"
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
    // .actor の名前と id だけを持つ配置物を doc へ足す
    void AddActorObject(nlohmann::json& doc, std::string_view actorFile, NS::uint32 id)
    {
        nlohmann::json object = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(object, actorFile);
        NS::SetObjectJsonId(object, id);
        NS::SceneJsonObjects(doc).push_back(std::move(object));
    }

    // 開始の手前で止まる ActorList の口を外から呼べるか。テンプレートにして、呼べない時を偽として受ける
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
    static_assert(!CanAppendWithNewIdFromOutside<NS::ActorList>);
    static_assert(!CanInsertFromJsonFromOutside<NS::ActorList>);
    static_assert(!CanSpawnFromOutside<NS::ActorList>);
}

TEST(SceneActor, ClassOnlyObjectBuildsWholeComposition)
{
    // 部品の一覧を持たないデータでも、クラスの OnInit が部品を全て積む
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "PropActor", 1);

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
    AddActorObject(doc, "PropActor", 1);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    GL::Level::PropActor* actor = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(actor, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(actor->Params(), nlohmann::json{{"質量", 1.5f}}), 0u);
    actor->Root().SetPosition(NS::Vector3{3.0f, 4.0f, 5.0f});

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_EQ(NS::ObjectJsonActorFile(objects[0]), "PropActor");

    // 読み直すと同じクラスで組まれ、変えた値が戻る
    NS::Scene reloaded;
    reloaded.ResetFromJson(saved);
    NS::SceneActor* again = reloaded.Objects().FindByObjectId(1);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(std::string_view{again->ClassName()}, "PropActor");
    const GL::Level::PropActor* propAgain = NS::Cast<GL::Level::PropActor>(again);
    ASSERT_NE(propAgain, nullptr);
    EXPECT_FLOAT_EQ(propAgain->Params().Mass(), 1.5f);
    EXPECT_FLOAT_EQ(again->Root().Position().x, 3.0f);
    EXPECT_FLOAT_EQ(again->Root().Position().y, 4.0f);
    EXPECT_FLOAT_EQ(again->Root().Position().z, 5.0f);
}

TEST(SceneActor, UnknownClassObjKeyIsWarnedAndOtherClassObjsAreRead)
{
    // クラスの表に無い鍵は警告して読み飛ばし、表にある部品の値は読む
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "PropActor", 1);
    NS::Scene scene;
    scene.ResetFromJson(doc);
    GL::Level::PropActor* actor = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(actor, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(actor->Params(), nlohmann::json{{"質量", 1.5f}}), 0u);

    nlohmann::json saved = scene.ToJson();
    nlohmann::json& objects = NS::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    NS::ObjectJsonClassObjs(objects[0])["NotInTheClass"] = nlohmann::json{{"Radius", 9.0f}};

    const TestLogCapture log;
    NS::Scene reloaded;
    reloaded.ResetFromJson(saved);
    EXPECT_EQ(log.WarningCount(), 1);
    const GL::Level::PropActor* again = NS::Cast<GL::Level::PropActor>(reloaded.Objects().FindByObjectId(1));
    ASSERT_NE(again, nullptr);
    EXPECT_FLOAT_EQ(again->Params().Mass(), 1.5f);
}

TEST(SceneActor, FileTextRoundTripKeepsClass)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "MapPartsActor", 1);
    AddActorObject(doc, "GoalActor", 2);

    const std::string text = NS::SerializeSceneToJson(doc);
    nlohmann::json back;
    ASSERT_TRUE(NS::DeserializeSceneFromJson(back, text));

    const nlohmann::json& objects = NS::SceneJsonObjects(back);
    ASSERT_EQ(objects.size(), 2u);
    EXPECT_EQ(NS::ObjectJsonActorFile(objects[NS::FindObjectIndexById(back, 1)]), "MapPartsActor");
    EXPECT_EQ(NS::ObjectJsonActorFile(objects[NS::FindObjectIndexById(back, 2)]), "GoalActor");
}

TEST(SceneActor, EnvironmentRoundTripsThroughFileAndScene)
{
    nlohmann::json doc = NS::MakeSceneJson();
    NS::WriteSceneEnvironment(doc, NS::SceneEnvironment{.skybox = "Assets/Skybox/kurt"});

    const std::string text = NS::SerializeSceneToJson(doc);
    nlohmann::json decoded;
    ASSERT_TRUE(NS::DeserializeSceneFromJson(decoded, text));
    const NS::SceneEnvironment read = NS::ReadSceneEnvironment(decoded);
    EXPECT_EQ(read.skybox, "Assets/Skybox/kurt");

    NS::Scene scene;
    scene.ResetFromJson(decoded);
    EXPECT_EQ(scene.SkyboxPath(), "Assets/Skybox/kurt");
    const NS::SceneEnvironment saved = NS::ReadSceneEnvironment(scene.ToJson());
    EXPECT_EQ(saved.skybox, "Assets/Skybox/kurt");
}

// environment の欄の名前と値は SceneEnvironment のリフレクションが決める
TEST(SceneActor, EnvironmentIsWrittenAndReadByTheReflectedFields)
{
    const NS::ReflectionInfo* info = NS::SceneEnvironment::StaticReflection();
    ASSERT_NE(info, nullptr);
    ASSERT_GT(info->fieldCount, 0u);
    const NS::SceneEnvironment written{.skybox = "Assets/Skybox/kurt"};
    const nlohmann::json fields = NS::SerializeReflectedFields(&written, *info);

    nlohmann::json doc = NS::MakeSceneJson();
    NS::WriteSceneEnvironment(doc, written);
    EXPECT_EQ(doc["environment"], fields);

    // リフレクションで書いた environment を読み直すと同じ値になり、警告を出さない
    const TestLogCapture log;
    nlohmann::json reflected = NS::MakeSceneJson();
    reflected["environment"] = fields;
    const NS::SceneEnvironment read = NS::ReadSceneEnvironment(reflected);
    EXPECT_EQ(NS::SerializeReflectedFields(&read, *info), fields);
    EXPECT_EQ(log.WarningCount(), 0);
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

TEST(SceneLoad, EveryShippedPlacementOverridesOnlyMarkedFields)
{
    // 配置物が持つ欄は、根の部品・参照の欄・上書きの印の付いた欄だけ。ほかの欄は読む時に捨てられる
    NS::ActorFileLibrary::Get().Reload();
    const std::filesystem::path folder = std::filesystem::path{NS::OS::FileSystem::ContentRoot()} / "Assets" / "Scenes";
    int checked = 0;
    for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator{folder})
    {
        if (file.path().extension() != ".scene")
        {
            continue;
        }
        nlohmann::json doc;
        ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, file.path().string())) << file.path().string();
        for (const nlohmann::json& object : NS::SceneJsonObjects(doc))
        {
            const NS::SceneActor& baseline = NS::ActorFileLibrary::Get().Baseline(NS::ObjectJsonActorFile(object));
            NS::ForEachReflectedClassObj(baseline, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
                const nlohmann::json* fields = NS::ClassObjFields(object, row.key);
                if (fields == nullptr || part == &baseline.Root())
                {
                    return;
                }
                for (nlohmann::json::const_iterator field = fields->begin(); field != fields->end(); ++field)
                {
                    const NS::FieldDesc* desc = NS::FindField(part->GetReflection(), field.key());
                    ASSERT_NE(desc, nullptr) << row.key << " " << field.key();
                    EXPECT_TRUE(desc->placement || desc->type == NS::FieldType::ActorRef)
                        << file.path().string() << " " << NS::ObjectJsonActorFile(object) << " " << row.key << " "
                        << field.key();
                    ++checked;
                }
            });
        }
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
    NS::WriteObjectHeader(object, NS::ObjectHeader{.id = 4, .actorFile = "PropActor", .name = "Ball", .active = false});
    const NS::ObjectHeader header = NS::ReadObjectHeader(object);
    EXPECT_EQ(header.id, 4u);
    EXPECT_EQ(header.actorFile, "PropActor");
    EXPECT_EQ(header.name, "Ball");
    EXPECT_FALSE(header.active);
}

TEST(SceneJson, NextFreeObjectIdStepsPastTheLargestId)
{
    // 手編集で nextObjectId が配置物の id より小さいファイルもある。次の id は最大の id の次より前に戻らない
    nlohmann::json doc = NS::MakeSceneJson();
    EXPECT_EQ(NS::NextFreeObjectId(doc), 1u);
    AddActorObject(doc, "PropActor", 5);
    NS::SetSceneJsonNextObjectId(doc, 2);
    EXPECT_EQ(NS::NextFreeObjectId(doc), 6u);
    NS::SetSceneJsonNextObjectId(doc, 10);
    EXPECT_EQ(NS::NextFreeObjectId(doc), 10u);
}

TEST(SceneActor, ApplyFromJsonNumbersANameThatIsAlreadyUsed)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "PropActor", 1);
    AddActorObject(doc, "PropActor", 2);
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
    // 出荷シーンの配置物はどれも見つかる .actor を指し、そのクラスは登録済み。作られない配置物が無い
    const std::string path = NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Scenes"),
        "new_scene.scene");
    nlohmann::json doc;
    ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, path));

    const nlohmann::json& objects = NS::SceneJsonObjects(doc);
    ASSERT_FALSE(objects.empty());
    for (const nlohmann::json& object : objects)
    {
        const std::string_view actorFile = NS::ObjectJsonActorFile(object);
        const std::string className = NS::ActorFileLibrary::Get().ClassNameOf(actorFile);
        ASSERT_FALSE(className.empty()) << NS::ObjectJsonName(object) << " " << actorFile;
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
        if (NS::ObjectJsonActorFile(object) == "PlayerActor")
        {
            ++players;
        }
        if (NS::ObjectJsonActorFile(object) == "DeathZoneActor")
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
    // PlayerActor.actor を試しの間だけ差し替え、終わったら元へ戻す
    class ScopedPlayerActorFile
    {
    public:
        explicit ScopedPlayerActorFile(nlohmann::json actorFile)
        {
            const nlohmann::json* current = NS::ActorFileLibrary::Get().Find("PlayerActor");
            if (current != nullptr)
            {
                m_previous = *current;
            }
            NS::ActorFileLibrary::Get().Set("PlayerActor", std::move(actorFile));
        }

        ~ScopedPlayerActorFile()
        {
            if (m_previous.has_value())
            {
                NS::ActorFileLibrary::Get().Set("PlayerActor", std::move(*m_previous));
            }
            else
            {
                NS::ActorFileLibrary::Get().Erase("PlayerActor");
            }
        }

        ScopedPlayerActorFile(const ScopedPlayerActorFile&) = delete;
        ScopedPlayerActorFile& operator=(const ScopedPlayerActorFile&) = delete;

    private:
        std::optional<nlohmann::json> m_previous;
    };
} // namespace

TEST(SceneActor, EnsuredPlayerStandsOnTheAssumedFloorForItsCapsule)
{
    // 補う自機の高さはカプセルの寸法から出す。床の上面 0.5 + 半分の高さ + 半径 + 余白 1cm
    // .actor の半径を変えても、足元が床の 1cm 上に出る
    const ScopedPlayerActorFile actorFile{nlohmann::json{
        {"class", "PlayerActor"}, {"classObjs", {{"Collider", {{"半径", 0.65f}, {"半分の高さ", 0.5f}}}}}}};
    nlohmann::json doc = NS::MakeSceneJson();
    EXPECT_TRUE(NS::Editor::EnsurePlayerObject(doc));

    const nlohmann::json& objects = NS::SceneJsonObjects(doc);
    ASSERT_EQ(objects.size(), std::size_t{1});
    EXPECT_NEAR(NS::Editor::ObjectPosition(objects[0]).y, 0.5f + 0.5f + 0.65f + 0.01f, 1e-5f);
}

TEST(SceneActor, EnsuredPlayerAndRestartWithoutBaselineShareTheDefaultSpawnPosition)
{
    // 補う位置とやり直しの落ち先は同じ DefaultSpawnPosition から出る。数字の写しが戻ると片方だけずれる
    const ScopedPlayerActorFile actorFile{nlohmann::json{
        {"class", "PlayerActor"}, {"classObjs", {{"Collider", {{"半径", 0.65f}, {"半分の高さ", 0.5f}}}}}}};
    nlohmann::json doc = NS::MakeSceneJson();
    ASSERT_TRUE(NS::Editor::EnsurePlayerObject(doc));
    const NS::Vector3 ensured = NS::Editor::ObjectPosition(NS::SceneJsonObjects(doc)[0]);

    NS::Scene scene;
    scene.ResetFromJson(doc);
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 fallback = player->DefaultSpawnPosition();
    EXPECT_FLOAT_EQ(ensured.y, fallback.y);

    // コースが始まる前のやり直しは、補う位置と同じ高さへ戻る
    player->Root().SetPosition(NS::Vector3{3.0f, 40.0f, 3.0f});
    player->Restart();
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

TEST(SceneLoad, RejectsVersionWhosePlacementsNameTheClass)
{
    // 版 8 の配置物は class にクラス名を書いていた。読むと .actor を指さない配置物が全部作られない
    nlohmann::json previous = NS::MakeSceneJson();
    previous["version"] = 8;
    nlohmann::json loaded;
    const TestLogCapture log;
    EXPECT_FALSE(NS::DeserializeSceneFromJson(loaded, previous.dump()));
    EXPECT_EQ(log.ErrorCount(), 1);
}

namespace
{
    // PropActor の半径だけを変えた、クラス名と違う名前で敷く .actor の中身
    nlohmann::json HeavyRockActorFile()
    {
        nlohmann::json actorFile = ClassOnlyActorFile("PropActor");
        NS::ObjectJsonClassObjs(actorFile)["Collision"] = nlohmann::json{{"半径", 2.5f}};
        return actorFile;
    }

    float RadiusOf(const NS::SceneActor& actor)
    {
        const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor.CollisionClassObj());
        if (sphere == nullptr)
        {
            return -1.0f;
        }
        return sphere->Radius();
    }
} // namespace

TEST(SceneLoad, PlacementBuildsTheClassAndValuesOfTheActorFileItNames)
{
    // 配置物の actor は .actor の名前。クラスは .actor の中の class から引き、.actor の値が敷かれる
    const ScopedActorFile heavy{"HeavyRock", HeavyRockActorFile()};
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "HeavyRock", 1);
    NS::Scene scene;
    scene.ResetFromJson(doc);

    const NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    EXPECT_NE(NS::Cast<GL::Level::PropActor>(actor), nullptr);
    EXPECT_EQ(actor->ActorFileName(), "HeavyRock");
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.5f);

    // 保存は同じ .actor を指す。比べる基準は .actor の名前ごとなので、.actor と同じ値は書かない
    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& object = NS::SceneJsonObjects(saved)[0];
    EXPECT_EQ(NS::ObjectJsonActorFile(object), "HeavyRock");
    const nlohmann::json* savedSphere = NS::ClassObjFields(object, "Collision");
    ASSERT_NE(savedSphere, nullptr);
    EXPECT_FALSE(savedSphere->contains("半径"));
}

TEST(SceneActor, SnapshotNamingAnotherActorFileRebuildsTheActor)
{
    // 同じクラスでも .actor が違えば敷く既定が違うので、控えを当てると作り直す
    const ScopedActorFile heavy{"HeavyRock", HeavyRockActorFile()};
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "PropActor", 1);
    NS::Scene scene;
    scene.ResetFromJson(doc);
    const NS::SceneActor* before = scene.Objects().FindByObjectId(1);
    ASSERT_NE(before, nullptr);

    nlohmann::json snapshot = NS::SceneJsonObjects(scene.ToJson())[0];
    NS::SetObjectJsonActorFile(snapshot, "HeavyRock");
    const NS::SceneActor* applied = scene.ApplyFromJson(snapshot);
    ASSERT_NE(applied, nullptr);
    EXPECT_NE(applied, before);
    EXPECT_EQ(applied->ActorFileName(), "HeavyRock");
    EXPECT_FLOAT_EQ(RadiusOf(*applied), 2.5f);
}

TEST(SceneLoad, PlacementWithoutItsActorFileIsKeptAndItsIdStaysTaken)
{
    // .actor の見つからない配置物は作らずに ERROR を出す。保存で消さず、その id を新しいアクターに振らない
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "PropActor", 1);
    AddActorObject(doc, "NoSuchActor", 7);
    nlohmann::json& lost = NS::SceneJsonObjects(doc)[1];
    NS::SetObjectJsonName(NS::SceneJsonObjects(doc)[0], "Rock");
    NS::SetObjectJsonName(lost, "Lost");
    NS::ObjectJsonClassObjs(lost)["Collision"] = nlohmann::json{{"半径", 3.0f}};
    NS::Editor::SetObjectPosition(lost, NS::Vector3{1.0f, 2.0f, 3.0f});
    NS::SetSceneJsonNextObjectId(doc, 2);

    const TestLogCapture log;
    NS::Scene scene;
    scene.ResetFromJson(doc);
    EXPECT_EQ(log.ErrorCount(), 1);
    EXPECT_EQ(scene.Objects().FindByObjectId(7), nullptr);
    EXPECT_EQ(scene.Objects().ObjectCount(), 1u);

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 2u);
    EXPECT_EQ(objects[1], NS::SceneJsonObjects(doc)[1]);

    const NS::SceneActor* added = scene.SpawnObject<GL::Level::PropActor>("Added");
    ASSERT_NE(added, nullptr);
    EXPECT_GT(added->Id(), 7u);
}

// 1 体だけ入れる道 (戻しの控えを当てる・入れる) も、.actor の見つからない配置物を読む時と同じく JSON のまま持つ。
// 持つ JSON は id ごとに 1 つで、その id を消すと消え、組めた時は持たない
TEST(SceneLoad, PlacementWithoutItsActorFileIsKeptOnEveryWayIn)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorObject(doc, "PropActor", 1);
    AddActorObject(doc, "PropActor", 2);
    NS::SetSceneJsonNextObjectId(doc, 3);
    const TestLogCapture log;
    NS::Scene scene;
    scene.ResetFromJson(doc);
    ASSERT_EQ(scene.Objects().ObjectCount(), 2u);

    nlohmann::json lostFirst = NS::SceneJsonObjects(doc)[0];
    NS::SetObjectJsonActorFile(lostFirst, "NoSuchActor");
    nlohmann::json lostSecond = NS::SceneJsonObjects(doc)[1];
    NS::SetObjectJsonActorFile(lostSecond, "NoSuchActor");

    // 作り直す道: アクターは消え、JSON が保存に残る。同じ id を 2 回当てても 1 つ
    EXPECT_EQ(scene.ApplyFromJson(lostFirst), nullptr);
    EXPECT_EQ(scene.ApplyFromJson(lostFirst), nullptr);
    // 入れる道
    scene.DestroyObject(2);
    EXPECT_EQ(scene.SpawnFromJson(lostSecond), nullptr);
    EXPECT_EQ(scene.Objects().ObjectCount(), 0u);
    {
        const nlohmann::json kept = scene.ToJson();
        const nlohmann::json& saved = NS::SceneJsonObjects(kept);
        ASSERT_EQ(saved.size(), 2u);
        EXPECT_EQ(saved[0], lostFirst);
        EXPECT_EQ(saved[1], lostSecond);
    }

    // 消すと持たない。組める控えを当てると、アクターに戻り JSON を持たない
    scene.DestroyObject(1);
    EXPECT_NE(scene.ApplyFromJson(NS::SceneJsonObjects(doc)[1]), nullptr);
    const nlohmann::json saved = scene.ToJson();
    ASSERT_EQ(NS::SceneJsonObjects(saved).size(), 1u);
    EXPECT_EQ(NS::ObjectJsonActorFile(NS::SceneJsonObjects(saved)[0]), "PropActor");
    EXPECT_EQ(NS::ObjectJsonId(NS::SceneJsonObjects(saved)[0]), 2u);
}

TEST(SceneActor, ActorMadeFromATypeAnswersTheActorFileOfItsClass)
{
    // 型から作るアクターは、クラスと同じ名前の .actor を敷く
    NS::Scene scene;
    const NS::SceneActor* spawned = scene.SpawnObject<GL::Level::PropActor>("Spawned");
    ASSERT_NE(spawned, nullptr);
    EXPECT_EQ(spawned->ActorFileName(), "PropActor");
    const std::unique_ptr<GL::Level::PropActor> made = NS::CreateActor<GL::Level::PropActor>();
    EXPECT_EQ(made->ActorFileName(), "PropActor");
}

// 欄の表示名と部品の登録が保存の鍵なので、鍵が変わると読み直した値が静かに既定へ戻る
TEST(SceneLoad, EveryFieldKeySurvivesWritingBack)
{
    // Assets/Actors の .actor を 1 本ずつ、全欄を書いた形で置く。読んで書き戻し、全欄へ広げると、置いた鍵が全部残る
    NS::ActorFileLibrary::Get().Reload();
    nlohmann::json doc = NS::MakeSceneJson();
    NS::uint32 id = 1;
    for (const std::string& name : NS::ActorFileLibrary::Get().Names())
    {
        nlohmann::json object = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(object, name);
        NS::SetObjectJsonId(object, id);
        ++id;
        NS::SceneJsonObjects(doc).push_back(NS::ExpandObjectJson(object));
    }
    ASSERT_FALSE(NS::SceneJsonObjects(doc).empty());
    NS::Scene scene;
    scene.ResetFromJson(doc);

    // 書き戻しは .actor と同じ欄を省くので、全欄へ広げてから、置いた鍵が全部残っているかを見る
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
            const std::string where = std::string{NS::ObjectJsonActorFile(fileObjects[i])} + " " + part.key();
            ASSERT_TRUE(writtenParts.contains(part.key())) << where;
            for (nlohmann::json::const_iterator field = part->begin(); field != part->end(); ++field)
            {
                EXPECT_TRUE(writtenParts[part.key()].contains(field.key())) << where << " " << field.key();
            }
        }
    }
}
