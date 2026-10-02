#include "Editor/LevelEditorController.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Reflection/Archetype.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Platform/StringUtils.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

// 値の 3 段の重なり (コードの既定値 < 種類の既定値 < 個体の上書き) と、差分の保存と、種類の既定にする操作を縛る

namespace
{
    // 種類の既定値の置き場を試しごとの空のディレクトリへ向け、終わったら元へ戻す
    // 前の実行が残したファイルは消してから始める
    class ScopedArchetypeDirectory
    {
    public:
        explicit ScopedArchetypeDirectory(std::string_view name)
            : m_previous(NS::Obj::ArchetypeLibrary::Get().Directory())
        {
            using NS::Platform::FileSystem;
            m_directory = FileSystem::Combine(
                FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "TestArchetypes"), name);
            (void)FileSystem::CreateDirectories(m_directory);
            for (const std::string& path : FileSystem::ListFiles(m_directory, ".json"))
            {
                std::error_code error;
                std::filesystem::remove(std::filesystem::path{NS::Platform::StringUtils::WideFromUtf8(path)}, error);
            }
            NS::Obj::ArchetypeLibrary::Get().SetDirectory(m_directory);
        }

        ~ScopedArchetypeDirectory() { NS::Obj::ArchetypeLibrary::Get().SetDirectory(m_previous); }

        ScopedArchetypeDirectory(const ScopedArchetypeDirectory&) = delete;
        ScopedArchetypeDirectory& operator=(const ScopedArchetypeDirectory&) = delete;

        [[nodiscard]] const std::string& Path() const noexcept { return m_directory; }

    private:
        std::string m_previous;
        std::string m_directory;
    };

    // 部品 1 つの種類の既定値を作る
    nlohmann::json ArchetypeWith(std::string_view className, std::string_view role, nlohmann::json entry)
    {
        nlohmann::json archetype = nlohmann::json::object();
        NS::Obj::SetObjectJsonClass(archetype, className);
        NS::Obj::ObjectJsonParts(archetype)[std::string{role}] = std::move(entry);
        return archetype;
    }

    nlohmann::json SphereEntry(float radius)
    {
        return nlohmann::json{{"半径", radius}};
    }

    nlohmann::json MapObjJson(std::uint32_t id)
    {
        nlohmann::json object = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(object, "MapObj");
        NS::Obj::SetObjectJsonId(object, id);
        return object;
    }

    float RadiusOf(const NS::Obj::Actor& actor)
    {
        const NS::Obj::SphereCollider* sphere =
            NS::Obj::ComponentCast<NS::Obj::SphereCollider>(actor.Part("Collision"));
        if (sphere == nullptr)
        {
            return -1.0f;
        }
        return sphere->Radius();
    }

    // 保存した配置物の JSON から、部品の件の fields を引く。無ければ nullptr
    const nlohmann::json* SavedFields(const nlohmann::json& object, std::string_view typeName)
    {
        return NS::Obj::PartFields(object, typeName);
    }
} // namespace

TEST(Archetype, CodeDefaultWithoutArchetype)
{
    const ScopedArchetypeDirectory scope("CodeDefault");
    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(MapObjJson(1), nullptr);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 0.5f);
    // 種類の既定値が無いので、種類が足す影も無い
    EXPECT_EQ(actor->ShadowPart(), nullptr);
}

TEST(Archetype, ArchetypeOverridesCodeDefault)
{
    const ScopedArchetypeDirectory scope("OverridesCode");
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(2.0f)));

    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(MapObjJson(1), nullptr);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.0f);
}

TEST(Archetype, InstanceOverridesArchetype)
{
    const ScopedArchetypeDirectory scope("InstanceOverrides");
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::Obj::ObjectJsonParts(object)["Collision"] = SphereEntry(3.0f);
    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(object, nullptr);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 3.0f);
}

TEST(Archetype, ArchetypeDecidesOptionalParts)
{
    const ScopedArchetypeDirectory scope("OptionalParts");
    nlohmann::json shadow = nlohmann::json::object();
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Shadow", std::move(shadow)));

    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(MapObjJson(1), nullptr);
    ASSERT_NE(actor, nullptr);
    EXPECT_NE(actor->ShadowPart(), nullptr);
}

TEST(Archetype, InstanceCannotAddParts)
{
    const ScopedArchetypeDirectory scope("InstanceParts");
    nlohmann::json object = MapObjJson(1);
    NS::Obj::ObjectJsonParts(object)["DirectionalLight"] = nlohmann::json::object();

    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(object, nullptr);
    ASSERT_NE(actor, nullptr);
    // どの部品を持つかはクラスと種類の既定値が決める
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::DirectionalLight>(actor->Part("DirectionalLight")), nullptr);
}

TEST(Archetype, ObjectWithoutClassIsNotBuilt)
{
    nlohmann::json object = NS::Obj::MakeObjectJson();
    NS::Obj::ObjectJsonParts(object)["Collision"] = SphereEntry(1.0f);
    EXPECT_EQ(NS::Obj::ObjectFromJson(object, nullptr), nullptr);
}

TEST(Archetype, SaveWritesOnlyOverrides)
{
    const ScopedArchetypeDirectory scope("SaveOverrides");
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Obj::SceneJsonObjects(doc).push_back(MapObjJson(2));
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    NS::Obj::Actor* changed = scene.Objects().FindByObjectId(2);
    ASSERT_NE(changed, nullptr);
    NS::Obj::ComponentCast<NS::Obj::SphereCollider>(changed->Part("Collision"))->SetRadius(4.0f);

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(saved);
    const nlohmann::json& plain = objects[NS::Obj::FindObjectIndexById(saved, 1)];
    const nlohmann::json& overridden = objects[NS::Obj::FindObjectIndexById(saved, 2)];

    // 部品の件は id と名前を保つため残り、種類の既定値と同じ欄は書かれない
    const nlohmann::json* plainSphere = NS::Obj::PartFields(plain, "Collision");
    ASSERT_NE(plainSphere, nullptr);
    EXPECT_FALSE(plainSphere->contains("id"));
    EXPECT_FALSE(NS::Obj::HasField(*plainSphere, "半径"));
    // 上書きした欄だけが書かれる
    const nlohmann::json* overriddenSphere = NS::Obj::PartFields(overridden, "Collision");
    ASSERT_NE(overriddenSphere, nullptr);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*overriddenSphere, "半径", 0.0f), 4.0f);
    // 位置は個体の物なので、既定と同じでも書く
    const nlohmann::json* transform = SavedFields(plain, "Transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(transform->contains("位置"));
}

TEST(Archetype, ChangedArchetypeReachesInstancesWithoutOverride)
{
    const ScopedArchetypeDirectory scope("ReachesInstances");
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(doc).push_back(MapObjJson(1));
    nlohmann::json overridden = MapObjJson(2);
    NS::Obj::ObjectJsonParts(overridden)["Collision"] = SphereEntry(4.0f);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(overridden));

    // 種類の既定値を変えてから読むと、上書きの無い個体だけが新しい値になる
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(6.0f)));
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 6.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 4.0f);
}

TEST(Archetype, ApplyingSnapshotResetsFieldsWithoutOverride)
{
    // undo の控えは上書きだけを持つ。控えを当てると、控えに無い欄も種類の既定値へ戻る
    const ScopedArchetypeDirectory scope("SnapshotReset");
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    NS::Obj::Actor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    const nlohmann::json snapshot = NS::Obj::ObjectToJson(*actor);

    NS::Obj::ComponentCast<NS::Obj::SphereCollider>(actor->Part("Collision"))->SetRadius(9.0f);
    NS::Obj::Actor* applied = scene.ApplyFromJson(snapshot);
    ASSERT_EQ(applied, actor); // 構成が同じなので作り直さない
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.0f);
}

TEST(Archetype, ExpandThenDiffGivesBackOverrides)
{
    const ScopedArchetypeDirectory scope("ExpandDiff");
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    nlohmann::json sphere = SphereEntry(3.0f);
    NS::Obj::SetObjectJsonId(object, 7u);
    NS::Obj::ObjectJsonParts(object)["Collision"] = std::move(sphere);

    const nlohmann::json full = NS::Obj::ExpandObjectJson(object);
    // 広げた姿は全ての部品と欄を持ち、個体の上書きと id を重ねている
    const nlohmann::json* fullSphere = NS::Obj::PartFields(full, "Collision");
    ASSERT_NE(fullSphere, nullptr);
    EXPECT_EQ(NS::Obj::ObjectJsonId(full), 7u);
    EXPECT_FALSE(fullSphere->contains("id"));
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*fullSphere, "半径", 0.0f), 3.0f);
    EXPECT_TRUE(NS::Obj::HasField(*fullSphere, "中心オフセット"));
    EXPECT_NE(NS::Obj::PartFields(full, "Params"), nullptr);

    const nlohmann::json back = NS::Obj::DiffObjectJson(full);
    const nlohmann::json* backSphere = NS::Obj::PartFields(back, "Collision");
    ASSERT_NE(backSphere, nullptr);
    EXPECT_FLOAT_EQ(NS::Obj::FieldFloat(*backSphere, "半径", 0.0f), 3.0f);
    EXPECT_FALSE(NS::Obj::HasField(*backSphere, "中心オフセット"));
}

TEST(Archetype, ReferenceFieldsAreNotArchetypeValues)
{
    const ScopedArchetypeDirectory scope("References");
    // 参照はシーンの中の相手を指すので、種類の既定値に書いても落ちる
    nlohmann::json follow = nlohmann::json::object();
    NS::Obj::SetField(follow, "追従対象", NS::Obj::ActorRef{5});
    NS::Obj::ArchetypeLibrary::Get().Set("FollowCamera", ArchetypeWith("FollowCamera", "Vcam", std::move(follow)));

    const nlohmann::json* archetype = NS::Obj::ArchetypeLibrary::Get().Find("FollowCamera");
    ASSERT_NE(archetype, nullptr);
    const nlohmann::json* entry = NS::Obj::PartFields(*archetype, "Vcam");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::Obj::HasField(*entry, "追従対象"));

    // インスペクタでも種類の既定にできない
    const NS::Obj::Actor& baseline = NS::Obj::ArchetypeLibrary::Get().Baseline("FollowCamera");
    const NS::Obj::ThirdPersonFollow* live = NS::Obj::ComponentCast<NS::Obj::ThirdPersonFollow>(baseline.Part("Vcam"));
    ASSERT_NE(live, nullptr);
    EXPECT_FALSE(NS::Obj::IsArchetypeField(*live, "追従対象"));
}

TEST(Archetype, OverrideIsDetectedAgainstBaseline)
{
    const ScopedArchetypeDirectory scope("OverrideDetect");
    NS::Obj::ArchetypeLibrary::Get().Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    NS::Obj::SphereCollider* sphere =
        NS::Obj::ComponentCast<NS::Obj::SphereCollider>(scene.Objects().FindByObjectId(1)->Part("Collision"));
    ASSERT_NE(sphere, nullptr);
    EXPECT_FALSE(NS::Obj::IsFieldOverridden(*sphere, "半径"));
    sphere->SetRadius(2.5f);
    EXPECT_TRUE(NS::Obj::IsFieldOverridden(*sphere, "半径"));
    EXPECT_TRUE(NS::Obj::IsArchetypeField(*sphere, "半径"));
}

TEST(Archetype, SavedArchetypeIsReadBack)
{
    const ScopedArchetypeDirectory scope("SaveReload");
    NS::Obj::ArchetypeLibrary& library = NS::Obj::ArchetypeLibrary::Get();
    library.Set("MapObj", ArchetypeWith("MapObj", "Collision", SphereEntry(1.25f)));
    ASSERT_TRUE(library.Save("MapObj"));
    EXPECT_TRUE(NS::Platform::FileSystem::Exists(NS::Platform::FileSystem::Combine(scope.Path(), "MapObj.json")));

    library.Erase("MapObj");
    EXPECT_EQ(library.Find("MapObj"), nullptr);
    library.Reload();
    const std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(MapObjJson(1), nullptr);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 1.25f);
}

TEST(Archetype, PromoteReachesOtherInstancesAndFile)
{
    const ScopedArchetypeDirectory scope("Promote");
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Obj::SceneJsonObjects(doc).push_back(MapObjJson(2));
    nlohmann::json keeps = MapObjJson(3);
    NS::Obj::ObjectJsonParts(keeps)["Collision"] = SphereEntry(4.0f);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(keeps));
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    LevelEditorController editor(&scene);

    NS::Obj::SphereCollider* sphere =
        NS::Obj::ComponentCast<NS::Obj::SphereCollider>(scene.Objects().FindByObjectId(1)->Part("Collision"));
    sphere->SetRadius(1.75f);
    ASSERT_TRUE(editor.PromoteFieldToArchetype(*sphere, "半径"));

    // 上げた個体は上書きでなくなり、上書きしていなかった個体は付いて来て、上書きしていた個体はそのまま
    EXPECT_FALSE(NS::Obj::IsFieldOverridden(*sphere, "半径"));
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 1.75f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(3)), 4.0f);
    EXPECT_TRUE(NS::Platform::FileSystem::Exists(NS::Platform::FileSystem::Combine(scope.Path(), "MapObj.json")));
}

TEST(Archetype, PromoteRejectsReferenceField)
{
    const ScopedArchetypeDirectory scope("PromoteReference");
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json camera = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(camera, "FollowCamera");
    NS::Obj::SetObjectJsonId(camera, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(camera));
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    LevelEditorController editor(&scene);

    NS::Obj::ThirdPersonFollow* follow =
        NS::Obj::ComponentCast<NS::Obj::ThirdPersonFollow>(scene.Objects().FindByObjectId(1)->Part("Vcam"));
    ASSERT_NE(follow, nullptr);
    EXPECT_FALSE(editor.PromoteFieldToArchetype(*follow, "追従対象"));
    EXPECT_EQ(NS::Obj::ArchetypeLibrary::Get().Find("FollowCamera"), nullptr);
}

TEST(Archetype, ShippedArchetypesLoad)
{
    // 出荷の種類の既定値はどれも登録済みのクラスの物で、置物は影を種類で足す
    NS::Obj::ArchetypeLibrary& library = NS::Obj::ArchetypeLibrary::Get();
    library.Reload();
    ASSERT_NE(library.Find("MapObj"), nullptr);
    ASSERT_NE(library.Find("Player"), nullptr);
    const NS::Obj::Actor& rock = library.Baseline("MapObj");
    EXPECT_NE(rock.ShadowPart(), nullptr);
}
