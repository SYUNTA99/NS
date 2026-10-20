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
#include "NSlib/CoreObject/Reflection/ActorFile.h"
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
#include "TestActorFile.h"
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
    class ScopedActorFileDirectory
    {
    public:
        explicit ScopedActorFileDirectory(std::string_view name) : m_previous(NS::ActorFileLibrary::Get().Directory())
        {
            using NS::OS::FileSystem;
            m_directory = FileSystem::Combine(
                FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "TestActorFiles"), name);
            (void)FileSystem::CreateDirectories(m_directory);
            for (const std::string& path : FileSystem::ListFiles(m_directory, ".actor"))
            {
                std::error_code error;
                std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
            }
            NS::ActorFileLibrary::Get().SetDirectory(m_directory);
        }

        ~ScopedActorFileDirectory() { NS::ActorFileLibrary::Get().SetDirectory(m_previous); }

        ScopedActorFileDirectory(const ScopedActorFileDirectory&) = delete;
        ScopedActorFileDirectory& operator=(const ScopedActorFileDirectory&) = delete;

        [[nodiscard]] const std::string& Path() const noexcept { return m_directory; }

    private:
        std::string m_previous;
        std::string m_directory;
    };

    // 部品 1 つの種類の既定値を作る
    nlohmann::json ActorFileWith(std::string_view className, std::string_view role, nlohmann::json entry)
    {
        nlohmann::json actorFile = nlohmann::json::object();
        NS::SetActorFileClass(actorFile, className);
        NS::ObjectJsonClassObjs(actorFile)[std::string{role}] = std::move(entry);
        return actorFile;
    }

    nlohmann::json SphereEntry(float radius)
    {
        return nlohmann::json{{"半径", radius}};
    }

    nlohmann::json MapObjJson(NS::uint32 id)
    {
        nlohmann::json object = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(object, "PropActor");
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

TEST(ActorFile, BaselineLoadsAssetsLikeAPlacedActor)
{
    const ScopedActorFileDirectory scope("BaselineLoadsAssets");
    const int before = BaselineLoadProbeActor::s_loads;
    (void)NS::ActorFileLibrary::Get().Baseline("BaselineLoadProbeActor");
    EXPECT_EQ(BaselineLoadProbeActor::s_loads, before + 1);
}

TEST(ActorFile, ClearingAssetsRebuildsTheBaseline)
{
    // 既定の 1 体は読んだ素材を指すので、素材の表を空にした後は作り直す
    const ScopedActorFileDirectory scope("ClearingAssetsRebuildsTheBaseline");
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    (void)library.Baseline("BaselineLoadProbeActor");
    const int before = BaselineLoadProbeActor::s_loads;
    assets.Clear();
    (void)library.Baseline("BaselineLoadProbeActor");
    EXPECT_EQ(BaselineLoadProbeActor::s_loads, before + 1);
}

TEST(ActorFile, CodeDefaultWhenTheActorFileHasNoValues)
{
    const ScopedActorFileDirectory scope("CodeDefault");
    const ScopedActorFile classOnly{"PropActor", ClassOnlyActorFile("PropActor")};
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 0.5f);
    EXPECT_NE(actor->ShadowClassObj(), nullptr);
}

TEST(ActorFile, ActorFileOverridesCodeDefault)
{
    const ScopedActorFileDirectory scope("OverridesCode");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.0f);
}

TEST(ActorFile, InstanceOverridesActorFile)
{
    const ScopedActorFileDirectory scope("InstanceOverrides");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonClassObjs(object)["Collision"] = SphereEntry(3.0f);
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 3.0f);
}

TEST(ActorFile, ActorFileCannotAddParts)
{
    const ScopedActorFileDirectory scope("FixedParts");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "AttackSensor", nlohmann::json::object()));

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->AttackSensorClassObj(), nullptr);
    EXPECT_NE(actor->ShadowClassObj(), nullptr);
}

TEST(ActorFile, InstanceCannotAddParts)
{
    const ScopedActorFileDirectory scope("InstanceParts");
    const ScopedActorFile classOnly{"PropActor", ClassOnlyActorFile("PropActor")};
    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonClassObjs(object)["DirectionalLight"] = nlohmann::json::object();

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(NS::ClassObjFields(NS::MakePrototypeJson(*actor), "DirectionalLight"), nullptr);
}

TEST(ActorFile, ObjectWithoutActorFileIsNotBuilt)
{
    nlohmann::json object = NS::MakeObjectJson();
    NS::ObjectJsonClassObjs(object)["Collision"] = SphereEntry(1.0f);
    NS::Scene scene;
    EXPECT_EQ(NS::ObjectFromJson(object, scene), nullptr);
}

// 型から湧かすアクターにも種類の既定が届く。作る道で型の値が変わらない
TEST(ActorFile, SpawnedByTypeGetsActorFile)
{
    const ScopedActorFileDirectory scope("SpawnedByType");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    NS::Scene scene;
    const NS::SceneActor* actor = scene.SpawnObject<GL::Level::PropActor>("PropActor");
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*actor), 2.0f);
}

TEST(ActorFile, SaveWritesOnlyOverrides)
{
    const ScopedActorFileDirectory scope("SaveOverrides");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

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

TEST(ActorFile, ChangedActorFileReachesInstancesWithoutOverride)
{
    const ScopedActorFileDirectory scope("ReachesInstances");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    nlohmann::json overridden = MapObjJson(2);
    NS::ObjectJsonClassObjs(overridden)["Collision"] = SphereEntry(4.0f);
    NS::SceneJsonObjects(doc).push_back(std::move(overridden));

    // 種類の既定値を変えてから読むと、上書きの無い個体だけが新しい値になる
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(6.0f)));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 6.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 4.0f);
}

TEST(ActorFile, ApplyingSnapshotResetsFieldsWithoutOverride)
{
    // undo の控えは上書きだけを持つ。控えを当てると、控えに無い欄も種類の既定値へ戻る
    const ScopedActorFileDirectory scope("SnapshotReset");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

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

TEST(ActorFile, ExpandThenDiffGivesBackOverrides)
{
    const ScopedActorFileDirectory scope("ExpandDiff");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

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

TEST(ActorFile, BaselineAndDiffFollowTheActorFileThePlacementNames)
{
    // 比べる基準は .actor の名前ごと。同じクラスでも、指す .actor の値と同じ欄だけが差分から落ちる
    const ScopedActorFileDirectory scope("DiffByName");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));
    const ScopedActorFile heavy{"HeavyRock", ActorFileWith("PropActor", "Collision", SphereEntry(3.0f))};

    const NS::SceneActor& baseline = NS::ActorFileLibrary::Get().Baseline("HeavyRock");
    EXPECT_EQ(std::string_view{baseline.ClassName()}, "PropActor");
    EXPECT_EQ(baseline.ActorFileName(), "HeavyRock");
    EXPECT_FLOAT_EQ(RadiusOf(baseline), 3.0f);

    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonClassObjs(object)["Collision"] = SphereEntry(3.0f);
    const nlohmann::json diffProp = NS::DiffObjectJson(object);
    const nlohmann::json* againstProp = NS::ClassObjFields(diffProp, "Collision");
    ASSERT_NE(againstProp, nullptr);
    EXPECT_TRUE(NS::HasField(*againstProp, "半径"));

    NS::SetObjectJsonActorFile(object, "HeavyRock");
    const nlohmann::json diffHeavy = NS::DiffObjectJson(object);
    const nlohmann::json* againstHeavy = NS::ClassObjFields(diffHeavy, "Collision");
    ASSERT_NE(againstHeavy, nullptr);
    EXPECT_FALSE(NS::HasField(*againstHeavy, "半径"));
}

TEST(ActorFile, ReferenceFieldsAreNotActorFileValues)
{
    const ScopedActorFileDirectory scope("References");
    // 参照はシーンの中の相手を指すので、種類の既定値に書いても落ちる
    nlohmann::json follow = nlohmann::json::object();
    NS::SetField(follow, "追従対象", NS::ActorRef{5});
    NS::ActorFileLibrary::Get().Set("FollowCameraActor", ActorFileWith("FollowCameraActor", "Vcam", std::move(follow)));

    const nlohmann::json* actorFile = NS::ActorFileLibrary::Get().Find("FollowCameraActor");
    ASSERT_NE(actorFile, nullptr);
    const nlohmann::json* entry = NS::ClassObjFields(*actorFile, "Vcam");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::HasField(*entry, "追従対象"));

    // インスペクタでも種類の既定にできない
    const NS::SceneActor& baseline = NS::ActorFileLibrary::Get().Baseline("FollowCameraActor");
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(&baseline);
    ASSERT_NE(camera, nullptr);
    const NS::ClassObjInfo* row = RowOf(*camera, &camera->Vcam());
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(NS::IsActorFileField(*camera, *row, "追従対象"));
}

TEST(ActorFile, OverrideIsDetectedAgainstBaseline)
{
    const ScopedActorFileDirectory scope("OverrideDetect");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

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
    EXPECT_TRUE(NS::IsActorFileField(actor, *row, "半径"));
}

// インスペクタの印と「既定に戻す」は、型の既定 (半径 0.5) でなく種類の既定 (半径 2) と比べて戻す
TEST(ActorFile, InspectorMarksAndRevertsAgainstTheActorFile)
{
    const ScopedActorFileDirectory scope("InspectorDefaults");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

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
TEST(ActorFile, InspectorDoesNotMarkAFieldTheActorFileChanged)
{
    const ScopedActorFileDirectory scope("InspectorWithoutBaselineRow");
    NS::ActorFileLibrary::Get().Set(
        "UnregisteredSphereActor", ActorFileWith("UnregisteredSphereActor", "Collision", SphereEntry(2.0f)));

    const std::unique_ptr<UnregisteredSphereActor> actor = NS::CreateActor<UnregisteredSphereActor>();
    NS::ApplyActorFile(*actor);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    ASSERT_FLOAT_EQ(sphere->Radius(), 2.0f);
    const NS::FieldDesc* radius = NS::FindField(sphere->GetReflection(), "半径");
    ASSERT_NE(radius, nullptr);
    const NS::ClassObjInfo* row = RowOf(*actor, sphere);
    ASSERT_NE(row, nullptr);

    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*sphere, NS::FindBaselineClassObj(*actor, *row), *radius));
}

TEST(ActorFile, TransformPartIsInstanceOnlyOnEveryPath)
{
    // 位置・回転・拡縮は個体の物。種類の既定値の読込・保存の差分・インスペクタの 3 つの道が同じ判断を通る
    const ScopedActorFileDirectory scope("TransformInstanceOnly");
    nlohmann::json written = nlohmann::json::object();
    NS::SetField(written, NS::k_PositionFieldName, NS::Vector3{5.0f, 6.0f, 7.0f});
    NS::ActorFileLibrary::Get().Set("PropActor",
                                    ActorFileWith("PropActor", "Transform", std::move(written)));
    const NS::SceneActor& baseline = NS::ActorFileLibrary::Get().Baseline("PropActor");
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
    EXPECT_FALSE(NS::IsActorFileField(baseline, *row, NS::k_PositionFieldName));
}

TEST(ActorFile, SavedActorFileIsReadBack)
{
    const ScopedActorFileDirectory scope("SaveReload");
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(1.25f)));
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

TEST(ActorFile, FileWithAnUnknownKeyIsNotRead)
{
    // 鍵を打ち間違えたファイルや古い鍵のファイルを読むと、値が黙って既定に戻る。読まずに ERROR で知らせる
    const ScopedActorFileDirectory scope("UnknownKey");
    nlohmann::json actorFile = ActorFileWith("PropActor", "Collision", SphereEntry(1.25f));
    actorFile["classObj"] = nlohmann::json::object();
    WriteActorFile(scope.Path(), "PropActor", actorFile.dump());

    const TestLogCapture log;
    NS::ActorFileLibrary::Get().Reload();
    EXPECT_EQ(NS::ActorFileLibrary::Get().Find("PropActor"), nullptr);
    EXPECT_EQ(log.ErrorCount(), 1);
}

TEST(ActorFile, PromoteReachesOtherInstancesAndFile)
{
    const ScopedActorFileDirectory scope("Promote");
    const ScopedActorFile classOnly{"PropActor", ClassOnlyActorFile("PropActor")};
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
    ASSERT_TRUE(editor.PromoteFieldToActorFile(actor, *row, "半径"));

    // 上げた個体は上書きでなくなり、上書きしていなかった個体は付いて来て、上書きしていた個体はそのまま
    EXPECT_FALSE(NS::IsFieldOverridden(actor, *row, "半径"));
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 1.75f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(3)), 4.0f);
    EXPECT_TRUE(NS::OS::FileSystem::Exists(NS::OS::FileSystem::Combine(scope.Path(), "PropActor.actor")));
}

TEST(ActorFile, PromoteRejectsReferenceField)
{
    const ScopedActorFileDirectory scope("PromoteReference");
    const ScopedActorFile classOnly{"FollowCameraActor", ClassOnlyActorFile("FollowCameraActor")};
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json camera = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(camera, "FollowCameraActor");
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
    EXPECT_FALSE(editor.PromoteFieldToActorFile(*placed, *row, "追従対象"));
    const nlohmann::json* actorFile = NS::ActorFileLibrary::Get().Find("FollowCameraActor");
    ASSERT_NE(actorFile, nullptr);
    EXPECT_EQ(NS::ClassObjFields(*actorFile, "Vcam"), nullptr);
}

TEST(ActorFile, ShippedActorFilesLoad)
{
    // 出荷の種類の既定値はどれも登録済みのクラスの物で、置物は影を種類で足す
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    ASSERT_NE(library.Find("PropActor"), nullptr);
    ASSERT_NE(library.Find("PlayerActor"), nullptr);
    const NS::SceneActor& rock = library.Baseline("PropActor");
    EXPECT_NE(rock.ShadowClassObj(), nullptr);
}

// 出荷の .actor は Save の形で書いてある。読んで書き直すとバイトまで同じで、前の置き場は残っていない
TEST(ActorFile, ShippedActorFilesRoundTripByteForByte)
{
    using NS::OS::FileSystem;
    const std::string assets = FileSystem::Combine(FileSystem::ContentRoot(), "Assets");
    EXPECT_FALSE(FileSystem::IsDirectory(FileSystem::Combine(assets, "ActorFiles")));
    const std::vector<std::string> paths = FileSystem::ListFiles(FileSystem::Combine(assets, "Actors"), ".actor");
    ASSERT_FALSE(paths.empty());

    const ScopedActorFileDirectory scope("ShippedRoundTrip");
    std::map<std::string, std::string> originals;
    for (const std::string& path : paths)
    {
        const std::optional<std::string> text = FileSystem::ReadAllText(path);
        ASSERT_TRUE(text.has_value()) << path;
        const std::string name = FileSystem::Stem(path);
        WriteActorFile(scope.Path(), name, *text);
        originals.emplace(name, *text);
    }
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
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
TEST(ActorFile, ClassIsReadFromInsideAndTheFileIsFoundByItsName)
{
    const ScopedActorFileDirectory scope("ClassFromInside");
    const nlohmann::json written = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f));
    WriteActorFile(scope.Path(), "HeavyRock", written.dump());

    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    const nlohmann::json* found = library.Find("HeavyRock");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(NS::ActorFileClass(*found), "PropActor");
    // 読んだ文書は頭の鍵を全部持ったまま返る
    EXPECT_EQ(*found, written);
    EXPECT_EQ(library.Find("PropActor"), nullptr);
}

// Set は頭の鍵を判じずに丸ごと持つ。class・classObjs の他の鍵も Find で返り、Save で書かれる
TEST(ActorFile, SetKeepsEveryHeadKeyThroughFindAndSave)
{
    using NS::OS::FileSystem;
    const ScopedActorFileDirectory scope("SetKeepsHeadKeys");
    nlohmann::json written = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f));
    written["hitReactions"] = nlohmann::json{{"Hit", 3}};

    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Set("HeavyRock", written);
    const nlohmann::json* found = library.Find("HeavyRock");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, written);

    ASSERT_TRUE(library.Save("HeavyRock"));
    const std::optional<std::string> text = FileSystem::ReadAllText(FileSystem::Combine(scope.Path(), "HeavyRock.actor"));
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(nlohmann::json::parse(*text), written);
}

// .actor の名前からクラスを引く。ファイル名と違う class でも中の class を返し、無い名前は空
TEST(ActorFile, ClassNameIsLookedUpByTheActorFileName)
{
    const ScopedActorFileDirectory scope("ClassNameByName");
    WriteActorFile(scope.Path(), "HeavyRock", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)).dump());

    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    EXPECT_EQ(library.ClassNameOf("HeavyRock"), "PropActor");
    EXPECT_TRUE(library.ClassNameOf("PropActor").empty());
    EXPECT_TRUE(library.ClassNameOf("NoSuchActor").empty());
}

// クラスの分からない .actor を読むと、どのクラスで作るかが黙って変わる。読まずに ERROR で知らせる
TEST(ActorFile, FileWithoutAClassOrWithAnUnregisteredClassIsNotRead)
{
    const ScopedActorFileDirectory scope("BadClass");
    nlohmann::json noClass = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f));
    noClass.erase("class");
    WriteActorFile(scope.Path(), "NoClass", noClass.dump());
    WriteActorFile(
        scope.Path(), "Unregistered", ActorFileWith("NoSuchActorClass", "Collision", SphereEntry(2.0f)).dump());

    const TestLogCapture log;
    NS::ActorFileLibrary::Get().Reload();
    EXPECT_EQ(NS::ActorFileLibrary::Get().Find("NoClass"), nullptr);
    EXPECT_EQ(NS::ActorFileLibrary::Get().Find("Unregistered"), nullptr);
    EXPECT_EQ(log.ErrorCount(), 2);
}
