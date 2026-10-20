#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
            for (const std::string& path : FileSystem::ListFiles(m_directory, ".actor"))
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
        NS::ObjectJsonClassObjs(archetype)[std::string{role}] = std::move(entry);
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
        const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor.CollisionClassObj());
        if (sphere == nullptr)
        {
            return -1.0f;
        }
        return sphere->Radius();
    }

    // actor の表で part を持つ行。無ければ nullptr
    const NS::ClassObjInfo* RowOf(const NS::SceneActor& actor, const NS::Object* part)
    {
        const NS::ClassObjInfo* row = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& field, const NS::Object* candidate) {
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
        return NS::ClassObjFields(object, typeName);
    }

    // directory に <name>.actor を text のまま書く
    void WriteActorFile(const std::string& directory, std::string_view name, const std::string& text)
    {
        const std::string path = NS::OS::FileSystem::Combine(directory, std::string{name} + ".actor");
        ASSERT_TRUE(
            NS::OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span<const char>{text.data(), text.size()})));
    }
} // namespace

// 素材を読み終えた回数を数える。既定の 1 体が配置と同じ初期化を通るかを見る
class BaselineLoadProbeActor final : public NS::SceneActor
{
public:
    static inline int s_loads = 0;

    NS_REFLECT_NONE(BaselineLoadProbeActor, NS::SceneActor)

protected:
    void OnAssetsLoaded() override
    {
        NS::SceneActor::OnAssetsLoaded();
        ++s_loads;
    }
};
NS_CLASS(BaselineLoadProbeActor)

TEST(Archetype, BaselineLoadsAssetsLikeAPlacedActor)
{
    const ScopedArchetypeDirectory scope("BaselineLoadsAssets");
    const int before = BaselineLoadProbeActor::s_loads;
    (void)NS::ArchetypeLibrary::Get().Baseline("BaselineLoadProbeActor");
    EXPECT_EQ(BaselineLoadProbeActor::s_loads, before + 1);
}

TEST(Archetype, ClearingAssetsRebuildsTheBaseline)
{
    // 既定の 1 体は読んだ素材を指すので、素材の表を空にした後は作り直す
    const ScopedArchetypeDirectory scope("ClearingAssetsRebuildsTheBaseline");
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    NS::ArchetypeLibrary& library = NS::ArchetypeLibrary::Get();
    (void)library.Baseline("BaselineLoadProbeActor");
    const int before = BaselineLoadProbeActor::s_loads;
    assets.Clear();
    (void)library.Baseline("BaselineLoadProbeActor");
    EXPECT_EQ(BaselineLoadProbeActor::s_loads, before + 1);
}

TEST(Archetype, CodeDefaultWithoutArchetype)
{
    const ScopedArchetypeDirectory scope("CodeDefault");
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 0.5f);
    EXPECT_NE(actor->ShadowClassObj(), nullptr);
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
    NS::ObjectJsonClassObjs(object)["Collision"] = SphereEntry(3.0f);
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
    EXPECT_EQ(actor->AttackSensorClassObj(), nullptr);
    EXPECT_NE(actor->ShadowClassObj(), nullptr);
}

TEST(Archetype, InstanceCannotAddParts)
{
    const ScopedArchetypeDirectory scope("InstanceParts");
    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonClassObjs(object)["DirectionalLight"] = nlohmann::json::object();

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(NS::ClassObjFields(NS::MakePrototypeJson(*actor), "DirectionalLight"), nullptr);
}

TEST(Archetype, ObjectWithoutClassIsNotBuilt)
{
    nlohmann::json object = NS::MakeObjectJson();
    NS::ObjectJsonClassObjs(object)["Collision"] = SphereEntry(1.0f);
    NS::Scene scene;
    EXPECT_EQ(NS::ObjectFromJson(object, scene), nullptr);
}

// 型から湧かすアクターにも種類の既定が届く。作る道で型の値が変わらない
TEST(Archetype, SpawnedByTypeGetsArchetype)
{
    const ScopedArchetypeDirectory scope("SpawnedByType");
    NS::ArchetypeLibrary::Get().Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f)));

    NS::Scene scene;
    const NS::SceneActor* actor = scene.SpawnObject<GL::Level::PropActor>("PropActor");
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
    NS::Cast<NS::SphereCollision>(changed->CollisionClassObj())->SetRadius(4.0f);

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonObjects(saved);
    const nlohmann::json& plain = objects[NS::FindObjectIndexById(saved, 1)];
    const nlohmann::json& overridden = objects[NS::FindObjectIndexById(saved, 2)];

    // 欄が全部既定と同じ部品も空の件として残り、種類の既定値と同じ欄は書かれない
    const nlohmann::json* plainSphere = NS::ClassObjFields(plain, "Collision");
    ASSERT_NE(plainSphere, nullptr);
    EXPECT_FALSE(plainSphere->contains("id"));
    EXPECT_FALSE(NS::HasField(*plainSphere, "半径"));
    // 上書きした欄だけが書かれる
    const nlohmann::json* overriddenSphere = NS::ClassObjFields(overridden, "Collision");
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
    NS::ObjectJsonClassObjs(overridden)["Collision"] = SphereEntry(4.0f);
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

    NS::Cast<NS::SphereCollision>(actor->CollisionClassObj())->SetRadius(9.0f);
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
    NS::ObjectJsonClassObjs(object)["Collision"] = std::move(sphere);

    const nlohmann::json full = NS::ExpandObjectJson(object);
    // 広げた姿は全ての部品と欄を持ち、個体の上書きと id を重ねている
    const nlohmann::json* fullSphere = NS::ClassObjFields(full, "Collision");
    ASSERT_NE(fullSphere, nullptr);
    EXPECT_EQ(NS::ObjectJsonId(full), 7u);
    EXPECT_FALSE(fullSphere->contains("id"));
    EXPECT_FLOAT_EQ(NS::FieldFloat(*fullSphere, "半径", 0.0f), 3.0f);
    EXPECT_TRUE(NS::HasField(*fullSphere, "中心オフセット"));
    EXPECT_NE(NS::ClassObjFields(full, "Params"), nullptr);

    const nlohmann::json back = NS::DiffObjectJson(full);
    const nlohmann::json* backSphere = NS::ClassObjFields(back, "Collision");
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
    const nlohmann::json* entry = NS::ClassObjFields(*archetype, "Vcam");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::HasField(*entry, "追従対象"));

    // インスペクタでも種類の既定にできない
    const NS::SceneActor& baseline = NS::ArchetypeLibrary::Get().Baseline("FollowCameraActor");
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(&baseline);
    ASSERT_NE(camera, nullptr);
    const NS::ClassObjInfo* row = RowOf(*camera, &camera->Vcam());
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
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(scene.Objects().FindByObjectId(1)->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    const NS::SceneActor& actor = *scene.Objects().FindByObjectId(1);
    const NS::ClassObjInfo* row = RowOf(actor, sphere);
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
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(scene.Objects().FindByObjectId(1)->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    const NS::FieldDesc* radius = NS::FindField(sphere->GetReflection(), "半径");
    ASSERT_NE(radius, nullptr);

    const NS::SceneActor& actor = *scene.Objects().FindByObjectId(1);
    const NS::ClassObjInfo* row = RowOf(actor, sphere);
    ASSERT_NE(row, nullptr);

    const NS::Object* baseline = NS::FindBaselineClassObj(actor, *row);
    ASSERT_NE(baseline, nullptr);
    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*sphere, baseline, *radius));
    sphere->SetRadius(2.5f);
    EXPECT_TRUE(NS::Editor::FieldDiffersFromDefault(*sphere, baseline, *radius));
    NS::Editor::RevertFieldToDefault(*sphere, *baseline, *radius);
    EXPECT_FLOAT_EQ(sphere->Radius(), 2.0f);
}

namespace
{
    // 登録の無いクラス。種類の既定の比べ先は素の SceneActor になり、Collision の行を持たない
    class UnregisteredSphereActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(UnregisteredSphereActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj<NS::Collision, NS::SphereCollision>(m_collision, this); }
    };
} // namespace

// 比べ先は種類の既定の比べ先の同じ行だけ。そこに行が無い時に型の既定 (半径 0.5) と比べると、
// 種類の既定で変えた半径 2 に印が出る
TEST(Archetype, InspectorDoesNotMarkAFieldTheArchetypeChanged)
{
    const ScopedArchetypeDirectory scope("InspectorWithoutBaselineRow");
    NS::ArchetypeLibrary::Get().Set(
        "UnregisteredSphereActor", ArchetypeWith("UnregisteredSphereActor", "Collision", SphereEntry(2.0f)));

    const std::unique_ptr<UnregisteredSphereActor> actor = NS::CreateActor<UnregisteredSphereActor>();
    NS::ApplyArchetype(*actor);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    ASSERT_FLOAT_EQ(sphere->Radius(), 2.0f);
    const NS::FieldDesc* radius = NS::FindField(sphere->GetReflection(), "半径");
    ASSERT_NE(radius, nullptr);
    const NS::ClassObjInfo* row = RowOf(*actor, sphere);
    ASSERT_NE(row, nullptr);

    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*sphere, NS::FindBaselineClassObj(*actor, *row), *radius));
}

TEST(Archetype, TransformPartIsInstanceOnlyOnEveryPath)
{
    // 位置・回転・拡縮は個体の物。種類の既定値の読込・保存の差分・インスペクタの 3 つの道が同じ判断を通る
    const ScopedArchetypeDirectory scope("TransformInstanceOnly");
    nlohmann::json written = nlohmann::json::object();
    NS::SetField(written, NS::k_PositionFieldName, NS::Vector3{5.0f, 6.0f, 7.0f});
    NS::ArchetypeLibrary::Get().Set("PropActor",
                                    ArchetypeWith("PropActor", "Transform", std::move(written)));
    const NS::SceneActor& baseline = NS::ArchetypeLibrary::Get().Baseline("PropActor");
    const NS::Vector3 position = baseline.Root().Position();
    EXPECT_FLOAT_EQ(position.x, 0.0f);
    EXPECT_FLOAT_EQ(position.y, 0.0f);
    EXPECT_FLOAT_EQ(position.z, 0.0f);

    // 既定と同じ位置でも保存の差分から落とさない
    const nlohmann::json back = NS::DiffObjectJson(NS::ExpandObjectJson(MapObjJson(1)));
    const nlohmann::json* transform = NS::ClassObjFields(back, "Transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(NS::HasField(*transform, NS::k_PositionFieldName));

    const NS::ClassObjInfo* row = RowOf(baseline, &baseline.Root());
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(NS::IsArchetypeField(baseline, *row, NS::k_PositionFieldName));
}

TEST(Archetype, SavedArchetypeIsReadBack)
{
    const ScopedArchetypeDirectory scope("SaveReload");
    NS::ArchetypeLibrary& library = NS::ArchetypeLibrary::Get();
    library.Set("PropActor", ArchetypeWith("PropActor", "Collision", SphereEntry(1.25f)));
    ASSERT_TRUE(library.Save("PropActor"));
    EXPECT_TRUE(NS::OS::FileSystem::Exists(NS::OS::FileSystem::Combine(scope.Path(), "PropActor.actor")));

    library.Erase("PropActor");
    EXPECT_EQ(library.Find("PropActor"), nullptr);
    library.Reload();
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 1.25f);
}

TEST(Archetype, FileWithAnUnknownKeyIsNotRead)
{
    // 鍵を打ち間違えたファイルや古い鍵のファイルを読むと、値が黙って既定に戻る。読まずに ERROR で知らせる
    const ScopedArchetypeDirectory scope("UnknownKey");
    nlohmann::json archetype = ArchetypeWith("PropActor", "Collision", SphereEntry(1.25f));
    archetype["classObj"] = nlohmann::json::object();
    WriteActorFile(scope.Path(), "PropActor", archetype.dump());

    const TestLogCapture log;
    NS::ArchetypeLibrary::Get().Reload();
    EXPECT_EQ(NS::ArchetypeLibrary::Get().Find("PropActor"), nullptr);
    EXPECT_EQ(log.ErrorCount(), 1);
}

TEST(Archetype, PromoteReachesOtherInstancesAndFile)
{
    const ScopedArchetypeDirectory scope("Promote");
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::SceneJsonObjects(doc).push_back(MapObjJson(2));
    nlohmann::json keeps = MapObjJson(3);
    NS::ObjectJsonClassObjs(keeps)["Collision"] = SphereEntry(4.0f);
    NS::SceneJsonObjects(doc).push_back(std::move(keeps));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);

    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(scene.Objects().FindByObjectId(1)->CollisionClassObj());
    sphere->SetRadius(1.75f);
    const NS::SceneActor& actor = *scene.Objects().FindByObjectId(1);
    const NS::ClassObjInfo* row = RowOf(actor, sphere);
    ASSERT_NE(row, nullptr);
    ASSERT_TRUE(editor.PromoteFieldToArchetype(actor, *row, "半径"));

    // 上げた個体は上書きでなくなり、上書きしていなかった個体は付いて来て、上書きしていた個体はそのまま
    EXPECT_FALSE(NS::IsFieldOverridden(actor, *row, "半径"));
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 1.75f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(3)), 4.0f);
    EXPECT_TRUE(NS::OS::FileSystem::Exists(NS::OS::FileSystem::Combine(scope.Path(), "PropActor.actor")));
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
    const NS::ClassObjInfo* row = RowOf(*placed, &placed->Vcam());
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
    EXPECT_NE(rock.ShadowClassObj(), nullptr);
}

// 出荷の .actor は Save の形で書いてある。読んで書き直すとバイトまで同じで、前の置き場は残っていない
TEST(Archetype, ShippedActorFilesRoundTripByteForByte)
{
    using NS::OS::FileSystem;
    const std::string assets = FileSystem::Combine(FileSystem::ContentRoot(), "Assets");
    EXPECT_FALSE(FileSystem::IsDirectory(FileSystem::Combine(assets, "Archetypes")));
    const std::vector<std::string> paths = FileSystem::ListFiles(FileSystem::Combine(assets, "Actors"), ".actor");
    ASSERT_FALSE(paths.empty());

    const ScopedArchetypeDirectory scope("ShippedRoundTrip");
    std::map<std::string, std::string> originals;
    for (const std::string& path : paths)
    {
        const std::optional<std::string> text = FileSystem::ReadAllText(path);
        ASSERT_TRUE(text.has_value()) << path;
        const std::string name = FileSystem::Stem(path);
        WriteActorFile(scope.Path(), name, *text);
        originals.emplace(name, *text);
    }
    NS::ArchetypeLibrary& library = NS::ArchetypeLibrary::Get();
    library.Reload();
    for (const std::pair<const std::string, std::string>& original : originals)
    {
        ASSERT_TRUE(library.Save(original.first)) << original.first;
        const std::optional<std::string> written =
            FileSystem::ReadAllText(FileSystem::Combine(scope.Path(), original.first + ".actor"));
        ASSERT_TRUE(written.has_value()) << original.first;
        EXPECT_EQ(*written, original.second) << original.first;
    }
}

// 名前はファイル名、クラスは中の class から読む。同じクラスに名前の違う既定を持てる
TEST(Archetype, ClassIsReadFromInsideAndTheFileIsFoundByItsName)
{
    const ScopedArchetypeDirectory scope("ClassFromInside");
    const nlohmann::json written = ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f));
    WriteActorFile(scope.Path(), "HeavyRock", written.dump());

    NS::ArchetypeLibrary& library = NS::ArchetypeLibrary::Get();
    library.Reload();
    const nlohmann::json* found = library.Find("HeavyRock");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(NS::ObjectJsonClass(*found), "PropActor");
    // 読んだ文書は頭の鍵を全部持ったまま返る
    EXPECT_EQ(*found, written);
    EXPECT_EQ(library.Find("PropActor"), nullptr);
}

// クラスの分からない .actor を読むと、どのクラスで作るかが黙って変わる。読まずに ERROR で知らせる
TEST(Archetype, FileWithoutAClassOrWithAnUnregisteredClassIsNotRead)
{
    const ScopedArchetypeDirectory scope("BadClass");
    nlohmann::json noClass = ArchetypeWith("PropActor", "Collision", SphereEntry(2.0f));
    noClass.erase("class");
    WriteActorFile(scope.Path(), "NoClass", noClass.dump());
    WriteActorFile(
        scope.Path(), "Unregistered", ArchetypeWith("NoSuchActorClass", "Collision", SphereEntry(2.0f)).dump());

    const TestLogCapture log;
    NS::ArchetypeLibrary::Get().Reload();
    EXPECT_EQ(NS::ArchetypeLibrary::Get().Find("NoClass"), nullptr);
    EXPECT_EQ(NS::ArchetypeLibrary::Get().Find("Unregistered"), nullptr);
    EXPECT_EQ(log.ErrorCount(), 2);
}
