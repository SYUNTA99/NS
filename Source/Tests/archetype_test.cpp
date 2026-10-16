#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Reflection/SubObjectEntry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/DirectionalLight.h"
#include "NSlib/CoreObject/SubObjects/Shadow.h"
#include "NSlib/CoreObject/SubObjects/SphereCollision.h"
#include "NSlib/CoreObject/SubObjects/ThirdPersonFollow.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
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
        explicit ScopedArchetypeDirectory(std::string_view name) : m_previous(NS::ArchetypeLibrary::Get().Directory())
        {
            using NS::OS::FileSystem;
            m_directory = FileSystem::Combine(
                FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "TestArchetypes"), name);
            (void)FileSystem::CreateDirectories(m_directory);
            for (const std::string& path : FileSystem::ListFiles(m_directory, ".json"))
            {
                std::error_code error;
                std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
            }
            NS::ArchetypeLibrary::Get().SetDirectory(m_directory);
        }

        ~ScopedArchetypeDirectory() { NS::ArchetypeLibrary::Get().SetDirectory(m_previous); }

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
        NS::SetObjectJsonClass(archetype, className);
        NS::ObjectJsonSubObjs(archetype)[std::string{role}] = std::move(entry);
        return archetype;
    }

    nlohmann::json SphereEntry(float radius)
    {
        return nlohmann::json{{"半径", radius}};
    }

    nlohmann::json MapObjJson(NS::uint32 id)
    {
        nlohmann::json object = NS::MakeObjectJson();
        NS::SetObjectJsonClass(object, "PropActor");
        NS::SetObjectJsonId(object, id);
        return object;
    }

    float RadiusOf(const NS::SceneActor& actor)
    {
        const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor.CollisionSubObj());
        if (sphere == nullptr)
        {
            return -1.0f;
        }
        return sphere->Radius();
    }

    // actor の表で part を持つ行。無ければ nullptr
    const NS::SubObjectDesc* RowOf(const NS::SceneActor& actor, const NS::SubObject* part)
    {
        const NS::SubObjectDesc* row = nullptr;
        NS::ForEachReflectedSubObj(actor, [&](const NS::SubObjectDesc& field, const NS::SubObject* candidate) {
            if (candidate == part)
            {
                row = &field;
            }
        });
        return row;
    }

    // 保存した配置物の JSON から、部品の件の fields を引く。無ければ nullptr
    const nlohmann::json* SavedFields(const nlohmann::json& object, std::string_view typeName)
    {
        return NS::SubObjFields(object, typeName);
    }
} // namespace

TEST(Archetype, CodeDefaultWithoutArchetype)
{
    const ScopedArchetypeDirectory scope("CodeDefault");
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 0.5f);
    EXPECT_NE(actor->ShadowSubObj(), nullptr);
}

TEST(Archetype, ArchetypeOverridesCodeDefault)
{
    const ScopedArchetypeDirectory scope("OverridesCode");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.0f);
}

TEST(Archetype, InstanceOverridesArchetype)
{
    const ScopedArchetypeDirectory scope("InstanceOverrides");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonSubObjs(object)["Collision"] = SphereEntry(3.0f);
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 3.0f);
}

TEST(Archetype, ArchetypeCannotAddParts)
{
    const ScopedArchetypeDirectory scope("FixedParts");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "AttackSensor", nlohmann::json::object()));

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->AttackSensorSubObj(), nullptr);
    EXPECT_NE(actor->ShadowSubObj(), nullptr);
}

TEST(Archetype, InstanceCannotAddParts)
{
    const ScopedArchetypeDirectory scope("InstanceParts");
    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonSubObjs(object)["DirectionalLight"] = nlohmann::json::object();

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(NS::SubObjFields(NS::MakePrototypeJson(*actor), "DirectionalLight"), nullptr);
}

TEST(Archetype, ObjectWithoutClassIsNotBuilt)
{
    nlohmann::json object = NS::MakeObjectJson();
    NS::ObjectJsonSubObjs(object)["Collision"] = SphereEntry(1.0f);
    NS::Scene scene;
    EXPECT_EQ(NS::ObjectFromJson(object, scene), nullptr);
}

// 型から湧かすアクターにも種類の既定が届く。作る道で型の値が変わらない
TEST(Archetype, SpawnedByTypeGetsArchetype)
{
    const ScopedArchetypeDirectory scope("SpawnedByType");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    NS::Scene scene;
    const NS::SceneActor* actor = scene.SpawnTransient<GL::Level::PropActor>();
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.0f);
}

TEST(Archetype, SaveWritesOnlyOverrides)
{
    const ScopedArchetypeDirectory scope("SaveOverrides");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::SceneJsonObjects(doc).push_back(MapObjJson(2));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* changed = scene.Objects().FindByObjectId(2);
    ASSERT_NE(changed, nullptr);
    NS::Cast<NS::SphereCollision>(changed->CollisionSubObj())->SetRadius(4.0f);

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonObjects(saved);
    const nlohmann::json& plain = objects[NS::FindObjectIndexById(saved, 1)];
    const nlohmann::json& overridden = objects[NS::FindObjectIndexById(saved, 2)];

    // 欄が全部既定と同じ部品も空の件として残り、種類の既定値と同じ欄は書かれない
    const nlohmann::json* plainSphere = NS::SubObjFields(plain, "Collision");
    ASSERT_NE(plainSphere, nullptr);
    EXPECT_FALSE(plainSphere->contains("id"));
    EXPECT_FALSE(NS::HasField(*plainSphere, "半径"));
    // 上書きした欄だけが書かれる
    const nlohmann::json* overriddenSphere = NS::SubObjFields(overridden, "Collision");
    ASSERT_NE(overriddenSphere, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*overriddenSphere, "半径", 0.0f), 4.0f);
    // 位置は個体の物なので、既定と同じでも書く
    const nlohmann::json* transform = SavedFields(plain, "Transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(transform->contains("位置"));
}

TEST(Archetype, ChangedArchetypeReachesInstancesWithoutOverride)
{
    const ScopedArchetypeDirectory scope("ReachesInstances");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    nlohmann::json overridden = MapObjJson(2);
    NS::ObjectJsonSubObjs(overridden)["Collision"] = SphereEntry(4.0f);
    NS::SceneJsonObjects(doc).push_back(std::move(overridden));

    // 種類の既定値を変えてから読むと、上書きの無い個体だけが新しい値になる
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(6.0f)));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 6.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 4.0f);
}

TEST(Archetype, ApplyingSnapshotResetsFieldsWithoutOverride)
{
    // undo の控えは上書きだけを持つ。控えを当てると、控えに無い欄も種類の既定値へ戻る
    const ScopedArchetypeDirectory scope("SnapshotReset");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    const nlohmann::json snapshot = NS::ObjectToJson(*actor);

    NS::Cast<NS::SphereCollision>(actor->CollisionSubObj())->SetRadius(9.0f);
    NS::SceneActor* applied = scene.ApplyFromJson(snapshot);
    ASSERT_EQ(applied, actor); // 構成が同じなので作り直さない
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.0f);
}

TEST(Archetype, ExpandThenDiffGivesBackOverrides)
{
    const ScopedArchetypeDirectory scope("ExpandDiff");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    nlohmann::json sphere = SphereEntry(3.0f);
    NS::SetObjectJsonId(object, 7u);
    NS::ObjectJsonSubObjs(object)["Collision"] = std::move(sphere);

    const nlohmann::json full = NS::ExpandObjectJson(object);
    // 広げた姿は全ての部品と欄を持ち、個体の上書きと id を重ねている
    const nlohmann::json* fullSphere = NS::SubObjFields(full, "Collision");
    ASSERT_NE(fullSphere, nullptr);
    EXPECT_EQ(NS::ObjectJsonId(full), 7u);
    EXPECT_FALSE(fullSphere->contains("id"));
    EXPECT_FLOAT_EQ(NS::FieldFloat(*fullSphere, "半径", 0.0f), 3.0f);
    EXPECT_TRUE(NS::HasField(*fullSphere, "中心オフセット"));
    EXPECT_NE(NS::SubObjFields(full, "Params"), nullptr);

    const nlohmann::json back = NS::DiffObjectJson(full);
    const nlohmann::json* backSphere = NS::SubObjFields(back, "Collision");
    ASSERT_NE(backSphere, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*backSphere, "半径", 0.0f), 3.0f);
    EXPECT_FALSE(NS::HasField(*backSphere, "中心オフセット"));
}

TEST(Archetype, ReferenceFieldsAreNotArchetypeValues)
{
    const ScopedArchetypeDirectory scope("References");
    // 参照はシーンの中の相手を指すので、種類の既定値に書いても落ちる
    nlohmann::json follow = nlohmann::json::object();
    NS::SetField(follow, "追従対象", NS::ActorRef{5});
    NS::ArchetypeLibrary::Get().Set("FollowCameraActor", ArchetypeWith("FollowCameraActor", "Vcam", std::move(follow)));

    const nlohmann::json* archetype = NS::ArchetypeLibrary::Get().Find("FollowCameraActor");
    ASSERT_NE(archetype, nullptr);
    const nlohmann::json* entry = NS::SubObjFields(*archetype, "Vcam");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::HasField(*entry, "追従対象"));

    // インスペクタでも種類の既定にできない
    const NS::SceneActor& baseline = NS::ArchetypeLibrary::Get().Baseline("FollowCameraActor");
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(&baseline);
    ASSERT_NE(camera, nullptr);
    const NS::SubObjectDesc* row = RowOf(*camera, &camera->Vcam());
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(NS::IsArchetypeField(*camera, *row, "追従対象"));
}

TEST(Archetype, OverrideIsDetectedAgainstBaseline)
{
    const ScopedArchetypeDirectory scope("OverrideDetect");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SphereCollision* sphere =
        NS::Cast<NS::SphereCollision>(scene.Objects().FindByObjectId(1)->CollisionSubObj());
    ASSERT_NE(sphere, nullptr);
    const NS::SceneActor& actor = *scene.Objects().FindByObjectId(1);
    const NS::SubObjectDesc* row = RowOf(actor, sphere);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(NS::IsFieldOverridden(actor, *row, "半径"));
    sphere->SetRadius(2.5f);
    EXPECT_TRUE(NS::IsFieldOverridden(actor, *row, "半径"));
    EXPECT_TRUE(NS::IsArchetypeField(actor, *row, "半径"));
}

// インスペクタの印と「既定に戻す」は、型の既定 (半径 0.5) でなく種類の既定 (半径 2) と比べて戻す
TEST(Archetype, InspectorMarksAndRevertsAgainstTheArchetype)
{
    const ScopedArchetypeDirectory scope("InspectorDefaults");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SphereCollision* sphere =
        NS::Cast<NS::SphereCollision>(scene.Objects().FindByObjectId(1)->CollisionSubObj());
    ASSERT_NE(sphere, nullptr);
    const NS::FieldDesc* radius = NS::FindField(sphere->GetReflection(), "半径");
    ASSERT_NE(radius, nullptr);

    const NS::SceneActor& actor = *scene.Objects().FindByObjectId(1);
    const NS::SubObjectDesc* row = RowOf(actor, sphere);
    ASSERT_NE(row, nullptr);

    NS::Editor::SubObjectDefaults defaults;
    const NS::SubObject* baseline = defaults.Find(actor, *row);
    ASSERT_NE(baseline, nullptr);
    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*sphere, baseline, *radius));
    sphere->SetRadius(2.5f);
    EXPECT_TRUE(NS::Editor::FieldDiffersFromDefault(*sphere, baseline, *radius));
    NS::Editor::RevertFieldToDefault(*sphere, *baseline, *radius);
    EXPECT_FLOAT_EQ(sphere->Radius(), 2.0f);
}

TEST(Archetype, TransformPartIsInstanceOnlyOnEveryPath)
{
    // 位置・回転・拡縮は個体の物。種類の既定値の読込・保存の差分・インスペクタの 3 つの道が同じ判断を通る
    const ScopedArchetypeDirectory scope("TransformInstanceOnly");
    nlohmann::json written = nlohmann::json::object();
    NS::SetField(written, NS::k_PositionFieldName, NS::Vector3{5.0f, 6.0f, 7.0f});
    NS::ArchetypeLibrary::Get().Set("PropActor",
                                    ArchetypeWith("PropActor", NS::k_TransformSubObjName, std::move(written)));
    const NS::SceneActor& baseline = NS::ArchetypeLibrary::Get().Baseline("PropActor");
    const NS::Vector3 position = baseline.Root().Position();
    EXPECT_FLOAT_EQ(position.x, 0.0f);
    EXPECT_FLOAT_EQ(position.y, 0.0f);
    EXPECT_FLOAT_EQ(position.z, 0.0f);

    // 既定と同じ位置でも保存の差分から落とさない
    const nlohmann::json back = NS::DiffObjectJson(NS::ExpandObjectJson(MapObjJson(1)));
    const nlohmann::json* transform = NS::SubObjFields(back, NS::k_TransformSubObjName);
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(NS::HasField(*transform, NS::k_PositionFieldName));

    const NS::SubObjectDesc* row = RowOf(baseline, &baseline.Root());
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(NS::IsArchetypeField(baseline, *row, NS::k_PositionFieldName));
}

TEST(Archetype, SavedArchetypeIsReadBack)
{
    const ScopedArchetypeDirectory scope("SaveReload");
    NS::ArchetypeLibrary& library = NS::ArchetypeLibrary::Get();
    library.Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(1.25f)));
    ASSERT_TRUE(library.Save("PropActor"));
    EXPECT_TRUE(NS::OS::FileSystem::Exists(NS::OS::FileSystem::Combine(scope.Path(), "PropActor.json")));

    library.Erase("PropActor");
    EXPECT_EQ(library.Find("PropActor"), nullptr);
    library.Reload();
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 1.25f);
}

TEST(Archetype, PromoteReachesOtherInstancesAndFile)
{
    const ScopedArchetypeDirectory scope("Promote");
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::SceneJsonObjects(doc).push_back(MapObjJson(2));
    nlohmann::json keeps = MapObjJson(3);
    NS::ObjectJsonSubObjs(keeps)["Collision"] = SphereEntry(4.0f);
    NS::SceneJsonObjects(doc).push_back(std::move(keeps));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);

    NS::SphereCollision* sphere =
        NS::Cast<NS::SphereCollision>(scene.Objects().FindByObjectId(1)->CollisionSubObj());
    sphere->SetRadius(1.75f);
    const NS::SceneActor& actor = *scene.Objects().FindByObjectId(1);
    const NS::SubObjectDesc* row = RowOf(actor, sphere);
    ASSERT_NE(row, nullptr);
    ASSERT_TRUE(editor.PromoteFieldToArchetype(actor, *row, "半径"));

    // 上げた個体は上書きでなくなり、上書きしていなかった個体は付いて来て、上書きしていた個体はそのまま
    EXPECT_FALSE(NS::IsFieldOverridden(actor, *row, "半径"));
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 1.75f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(3)), 4.0f);
    EXPECT_TRUE(NS::OS::FileSystem::Exists(NS::OS::FileSystem::Combine(scope.Path(), "PropActor.json")));
}

TEST(Archetype, PromoteRejectsReferenceField)
{
    const ScopedArchetypeDirectory scope("PromoteReference");
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json camera = NS::MakeObjectJson();
    NS::SetObjectJsonClass(camera, "FollowCameraActor");
    NS::SetObjectJsonId(camera, 1);
    NS::SceneJsonObjects(doc).push_back(std::move(camera));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);

    GL::Level::FollowCameraActor* placed =
        NS::Cast<GL::Level::FollowCameraActor>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(placed, nullptr);
    const NS::SubObjectDesc* row = RowOf(*placed, &placed->Vcam());
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(editor.PromoteFieldToArchetype(*placed, *row, "追従対象"));
    EXPECT_EQ(NS::ArchetypeLibrary::Get().Find("FollowCameraActor"), nullptr);
}

TEST(Archetype, ShippedArchetypesLoad)
{
    // 出荷の種類の既定値はどれも登録済みのクラスの物で、置物は影を種類で足す
    NS::ArchetypeLibrary& library = NS::ArchetypeLibrary::Get();
    library.Reload();
    ASSERT_NE(library.Find("PropActor"), nullptr);
    ASSERT_NE(library.Find("PlayerActor"), nullptr);
    const NS::SceneActor& rock = library.Baseline("PropActor");
    EXPECT_NE(rock.ShadowSubObj(), nullptr);
}
