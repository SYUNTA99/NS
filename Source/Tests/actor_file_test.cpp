#include "Editor/EditorAppearance.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
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

// 値の 3 段の重なり (コードの既定値 < .actor の値 < 配置物の印の付いた欄) と、差分の保存と、エディタの書く先を縛る

namespace
{
    // 部品 1 つだけに値を書いた .actor の文書を作る
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

    // 置物の質量の欄。配置物で上書きできる印が付いている
    nlohmann::json MassEntry(float mass)
    {
        return nlohmann::json{{"質量", mass}};
    }

    float MassOf(const NS::SceneActor& actor)
    {
        const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(&actor);
        if (prop == nullptr)
        {
            return -1.0f;
        }
        return prop->Params().Mass();
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
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Params", MassEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonClassObjs(object)["Params"] = MassEntry(3.0f);
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(MassOf(*actor), 3.0f);
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

// 型から湧かすアクターにもクラスと同じ名前の .actor が届く。作る道で型の値が変わらない
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
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Params", MassEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::SceneJsonObjects(doc).push_back(MapObjJson(2));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    GL::Level::PropActor* changed = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(2));
    ASSERT_NE(changed, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(changed->Params(), MassEntry(4.0f)), 0u);

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonObjects(saved);
    const nlohmann::json& plain = objects[NS::FindObjectIndexById(saved, 1)];
    const nlohmann::json& overridden = objects[NS::FindObjectIndexById(saved, 2)];

    // 欄が全部既定と同じ部品も空の件として残り、.actor の値と同じ欄は書かれない
    const nlohmann::json* plainParams = NS::ClassObjFields(plain, "Params");
    ASSERT_NE(plainParams, nullptr);
    EXPECT_FALSE(plainParams->contains("id"));
    EXPECT_FALSE(NS::HasField(*plainParams, "質量"));
    // 上書きした欄だけが書かれる
    const nlohmann::json* overriddenParams = NS::ClassObjFields(overridden, "Params");
    ASSERT_NE(overriddenParams, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*overriddenParams, "質量", 0.0f), 4.0f);
    // 位置は個体の物なので、既定と同じでも書く
    const nlohmann::json* transform = SavedFields(plain, "Transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(transform->contains("位置"));
}

TEST(ActorFile, ChangedActorFileReachesInstancesWithoutOverride)
{
    const ScopedActorFileDirectory scope("ReachesInstances");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Params", MassEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    nlohmann::json overridden = MapObjJson(2);
    NS::ObjectJsonClassObjs(overridden)["Params"] = MassEntry(4.0f);
    NS::SceneJsonObjects(doc).push_back(std::move(overridden));

    // .actor を変えてから読むと、上書きの無いアクターだけが新しい値になる
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Params", MassEntry(6.0f)));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(1)), 6.0f);
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(2)), 4.0f);
}

TEST(ActorFile, ApplyingSnapshotResetsFieldsWithoutOverride)
{
    // undo の控えは上書きだけを持つ。控えを当てると、控えに無い欄も .actor の値へ戻る
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
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Params", MassEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::SetObjectJsonId(object, 7u);
    NS::ObjectJsonClassObjs(object)["Params"] = MassEntry(3.0f);

    const nlohmann::json full = NS::ExpandObjectJson(object);
    // 広げた姿は全ての部品と欄を持ち、個体の上書きと id を重ねている
    const nlohmann::json* fullParams = NS::ClassObjFields(full, "Params");
    ASSERT_NE(fullParams, nullptr);
    EXPECT_EQ(NS::ObjectJsonId(full), 7u);
    EXPECT_FALSE(fullParams->contains("id"));
    EXPECT_FLOAT_EQ(NS::FieldFloat(*fullParams, "質量", 0.0f), 3.0f);
    EXPECT_TRUE(NS::HasField(*fullParams, "摩擦"));
    EXPECT_NE(NS::ClassObjFields(full, "Collision"), nullptr);

    const nlohmann::json back = NS::DiffObjectJson(full);
    const nlohmann::json* backParams = NS::ClassObjFields(back, "Params");
    ASSERT_NE(backParams, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*backParams, "質量", 0.0f), 3.0f);
    EXPECT_FALSE(NS::HasField(*backParams, "摩擦"));
}

TEST(ActorFile, BaselineAndDiffFollowTheActorFileThePlacementNames)
{
    // 比べる基準は .actor の名前ごと。同じクラスでも、指す .actor の値と同じ欄だけが差分から落ちる
    const ScopedActorFileDirectory scope("DiffByName");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Params", MassEntry(2.0f)));
    const ScopedActorFile heavy{"HeavyRock", ActorFileWith("PropActor", "Params", MassEntry(3.0f))};

    const NS::SceneActor& baseline = NS::ActorFileLibrary::Get().Baseline("HeavyRock");
    EXPECT_EQ(std::string_view{baseline.ClassName()}, "PropActor");
    EXPECT_EQ(baseline.ActorFileName(), "HeavyRock");
    EXPECT_FLOAT_EQ(MassOf(baseline), 3.0f);

    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonClassObjs(object)["Params"] = MassEntry(3.0f);
    const nlohmann::json diffProp = NS::DiffObjectJson(object);
    const nlohmann::json* againstProp = NS::ClassObjFields(diffProp, "Params");
    ASSERT_NE(againstProp, nullptr);
    EXPECT_TRUE(NS::HasField(*againstProp, "質量"));

    NS::SetObjectJsonActorFile(object, "HeavyRock");
    const nlohmann::json diffHeavy = NS::DiffObjectJson(object);
    const nlohmann::json* againstHeavy = NS::ClassObjFields(diffHeavy, "Params");
    ASSERT_NE(againstHeavy, nullptr);
    EXPECT_FALSE(NS::HasField(*againstHeavy, "質量"));
}

namespace
{
    // info の欄 name に配置物の上書きの印が付いているか。欄が無ければ偽
    bool IsMarked(const NS::ReflectionInfo* info, std::string_view name)
    {
        const NS::FieldDesc* field = NS::FindField(info, name);
        return field != nullptr && field->placement;
    }

    int MarkedCount(const NS::ReflectionInfo* info)
    {
        int count = 0;
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (info->fields[i].placement)
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

// 印は今のシーンが .actor と違う値を持つ 5 つの欄と、地形の形を配置物ごとに分けるメッシュの欄に付く
TEST(ActorFile, PlacementMarksAreOnTheSixFieldsScenesOverride)
{
    const NS::ReflectionInfo* params = GL::Level::MapObjParams::StaticReflection();
    EXPECT_TRUE(IsMarked(params, "耐久"));
    EXPECT_TRUE(IsMarked(params, "質量"));
    EXPECT_EQ(MarkedCount(params), 2);

    const NS::ReflectionInfo* model = NS::Model::StaticReflection();
    EXPECT_TRUE(IsMarked(model, "基本色"));
    EXPECT_TRUE(IsMarked(model, "マテリアル"));
    EXPECT_TRUE(IsMarked(model, "メッシュ"));
    EXPECT_EQ(MarkedCount(model), 3);

    const NS::ReflectionInfo* shadow = NS::Shadow::StaticReflection();
    EXPECT_TRUE(IsMarked(shadow, "基本直径"));
    EXPECT_EQ(MarkedCount(shadow), 1);

    EXPECT_FALSE(IsMarked(NS::SphereCollision::StaticReflection(), "半径"));
}

// 印の付いた欄は配置物の値が効き、保存で残る。印の無い欄は WARN を出して .actor の値のまま、保存で消える
TEST(ActorFile, OnlyMarkedFieldsAreReadAndWrittenFromThePlacement)
{
    const ScopedActorFileDirectory scope("PlacementMarks");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::ObjectJsonClassObjs(object)["Collision"] = SphereEntry(3.0f);
    NS::ObjectJsonClassObjs(object)["Params"] = nlohmann::json{{"質量", 8.0f}};
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(object);

    NS::Scene scene;
    {
        const TestLogCapture log;
        scene.ResetFromJson(doc);
        EXPECT_EQ(log.WarningCount(), 1);
    }
    GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(prop, nullptr);
    EXPECT_FLOAT_EQ(prop->Params().Mass(), 8.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*prop), 2.0f);

    // 控えを当てる道でも、印の無い欄の値は効かない
    nlohmann::json snapshot = NS::ObjectToJson(*prop);
    NS::ObjectJsonClassObjs(snapshot)["Collision"] = SphereEntry(5.0f);
    ASSERT_EQ(scene.ApplyFromJson(snapshot), prop);
    EXPECT_FLOAT_EQ(RadiusOf(*prop), 2.0f);

    // 印の無い欄は、アクターの値が .actor と違っても配置物に書かない
    NS::Cast<NS::SphereCollision>(prop->CollisionClassObj())->SetRadius(4.0f);
    const nlohmann::json saved = NS::SceneJsonObjects(scene.ToJson())[0];
    const nlohmann::json* savedParams = NS::ClassObjFields(saved, "Params");
    ASSERT_NE(savedParams, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*savedParams, "質量", 0.0f), 8.0f);
    const nlohmann::json* savedSphere = NS::ClassObjFields(saved, "Collision");
    ASSERT_NE(savedSphere, nullptr);
    EXPECT_FALSE(NS::HasField(*savedSphere, "半径"));
}

// 参照の欄と根の部品の値は、印が無くても配置物から読み書きされる
TEST(ActorFile, ReferenceAndRootFieldsComeFromThePlacementWithoutAMark)
{
    const ScopedActorFileDirectory scope("PlacementRefAndRoot");
    const ScopedActorFile classOnly{"FollowCameraActor", ClassOnlyActorFile("FollowCameraActor")};

    nlohmann::json object = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(object, "FollowCameraActor");
    NS::SetObjectJsonId(object, 2);
    nlohmann::json follow = nlohmann::json::object();
    NS::SetField(follow, "追従対象", NS::ActorRef{1});
    NS::ObjectJsonClassObjs(object)["Vcam"] = std::move(follow);
    NS::SetObjectPosition(object, NS::Vector3{1.0f, 2.0f, 3.0f});

    const TestLogCapture log;
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(log.WarningCount(), 0);
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(actor.get());
    ASSERT_NE(camera, nullptr);
    EXPECT_EQ(camera->Vcam().TargetRef(), NS::ActorRef{1});
    EXPECT_FLOAT_EQ(actor->Root().Position().y, 2.0f);

    const nlohmann::json saved = NS::ObjectToJson(*actor);
    const nlohmann::json* savedVcam = NS::ClassObjFields(saved, "Vcam");
    ASSERT_NE(savedVcam, nullptr);
    EXPECT_TRUE(NS::HasField(*savedVcam, "追従対象"));
    EXPECT_FLOAT_EQ(NS::ObjectPosition(saved).z, 3.0f);
}

TEST(ActorFile, ReferenceFieldsAreNotActorFileValues)
{
    const ScopedActorFileDirectory scope("References");
    // 参照はシーンの中の相手を指すので、.actor に書いても落ちる
    nlohmann::json follow = nlohmann::json::object();
    NS::SetField(follow, "追従対象", NS::ActorRef{5});
    NS::ActorFileLibrary::Get().Set("FollowCameraActor", ActorFileWith("FollowCameraActor", "Vcam", std::move(follow)));

    const nlohmann::json* actorFile = NS::ActorFileLibrary::Get().Find("FollowCameraActor");
    ASSERT_NE(actorFile, nullptr);
    const nlohmann::json* entry = NS::ClassObjFields(*actorFile, "Vcam");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(NS::HasField(*entry, "追従対象"));

    // インスペクタで直しても .actor へは書かず、配置物へ書く
    const NS::SceneActor& baseline = NS::ActorFileLibrary::Get().Baseline("FollowCameraActor");
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(&baseline);
    ASSERT_NE(camera, nullptr);
    const NS::FieldDesc* target = NS::FindField(camera->Vcam().GetReflection(), "追従対象");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(NS::FieldDestinationOf(*camera, camera->Vcam(), *target), NS::FieldDestination::Placement);
}

// 字の色の印は、配置物へ書く欄の値が .actor と違う時だけ付く。.actor へ書く欄には付かない
TEST(ActorFile, OverrideIsShownOnlyOnFieldsThePlacementWrites)
{
    const ScopedActorFileDirectory scope("OverrideDetect");
    nlohmann::json actorFile = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f));
    NS::ObjectJsonClassObjs(actorFile)["Params"] = MassEntry(2.0f);
    NS::ActorFileLibrary::Get().Set("PropActor", std::move(actorFile));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(prop, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(prop->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    const NS::ClassObjInfo* sphereRow = RowOf(*prop, sphere);
    const NS::ClassObjInfo* paramsRow = RowOf(*prop, &prop->Params());
    ASSERT_NE(sphereRow, nullptr);
    ASSERT_NE(paramsRow, nullptr);
    const NS::FieldDesc* radius = NS::FindField(sphere->GetReflection(), "半径");
    const NS::FieldDesc* mass = NS::FindField(prop->Params().GetReflection(), "質量");
    ASSERT_NE(radius, nullptr);
    ASSERT_NE(mass, nullptr);
    const NS::Object* sphereBaseline = NS::FindBaselineClassObj(*prop, *sphereRow);
    const NS::Object* paramsBaseline = NS::FindBaselineClassObj(*prop, *paramsRow);

    EXPECT_FALSE(NS::Editor::ShowsOverride(prop->Params(), paramsBaseline, *mass, prop));
    ASSERT_EQ(NS::ApplyJsonFields(prop->Params(), MassEntry(4.0f)), 0u);
    EXPECT_TRUE(NS::Editor::ShowsOverride(prop->Params(), paramsBaseline, *mass, prop));

    sphere->SetRadius(2.5f);
    EXPECT_FALSE(NS::Editor::ShowsOverride(*sphere, sphereBaseline, *radius, prop));
}

// 欄の書く先は 1 つの関数が決め、行の地の色は同じ答えの方の色になる
TEST(ActorFile, EachFieldKindHasOneDestinationAndTheRowColorFollowsIt)
{
    const ScopedActorFileDirectory scope("FieldDestination");
    const ScopedActorFile propFile{"PropActor", ClassOnlyActorFile("PropActor")};
    const ScopedActorFile cameraFile{"FollowCameraActor", ClassOnlyActorFile("FollowCameraActor")};
    const NS::SceneActor& prop = NS::ActorFileLibrary::Get().Baseline("PropActor");
    const GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(&prop);
    const NS::SceneActor& cameraActor = NS::ActorFileLibrary::Get().Baseline("FollowCameraActor");
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(&cameraActor);
    ASSERT_NE(rock, nullptr);
    ASSERT_NE(camera, nullptr);
    ASSERT_NE(rock->CollisionClassObj(), nullptr);

    struct Case
    {
        const NS::SceneActor* actor;
        const NS::Object* part;
        const char* field;
        NS::FieldDestination expected;
    };
    const Case cases[] = {
        {rock, rock->CollisionClassObj(), "半径", NS::FieldDestination::ActorFile},
        {rock, &rock->Params(), "質量", NS::FieldDestination::Placement},
        {camera, &camera->Vcam(), "追従対象", NS::FieldDestination::Placement},
        {rock, &rock->Root(), NS::k_PositionFieldName, NS::FieldDestination::Placement},
    };
    NS::Editor::EditorAppearance appearance;
    ASSERT_NE(appearance.actorFileRowColor, appearance.placementRowColor);
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.field);
        const NS::FieldDesc* field = NS::FindField(c.part->GetReflection(), c.field);
        ASSERT_NE(field, nullptr);
        const NS::FieldDestination destination = NS::FieldDestinationOf(*c.actor, *c.part, *field);
        EXPECT_EQ(destination, c.expected);
        NS::Color expectedColor = appearance.placementRowColor;
        if (destination == NS::FieldDestination::ActorFile)
        {
            expectedColor = appearance.actorFileRowColor;
        }
        EXPECT_EQ(NS::Editor::FieldRowColor(appearance, *c.actor, *c.part, *field), expectedColor);
    }
}

// インスペクタの印と「既定に戻す」は、型の既定 (半径 0.5) でなく .actor の値 (半径 2) と比べて戻す
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
    // 登録の無いクラス。.actor の比べる基準は素の SceneActor になり、Collision の行を持たない
    class UnregisteredSphereActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(UnregisteredSphereActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj<NS::Collision, NS::SphereCollision>(m_collision, this); }
    };
} // namespace

// 比べ先は .actor の比べる基準の同じ行だけ。そこに行が無い時に型の既定 (半径 0.5) と比べると、
// 型の既定と違う半径 2 に印が出る
TEST(ActorFile, InspectorDoesNotMarkAFieldTheBaselineDoesNotHave)
{
    const ScopedActorFileDirectory scope("InspectorWithoutBaselineRow");
    const std::unique_ptr<UnregisteredSphereActor> actor = NS::CreateActor<UnregisteredSphereActor>();
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    sphere->SetRadius(2.0f);
    ASSERT_FLOAT_EQ(sphere->Radius(), 2.0f);
    const NS::FieldDesc* radius = NS::FindField(sphere->GetReflection(), "半径");
    ASSERT_NE(radius, nullptr);
    const NS::ClassObjInfo* row = RowOf(*actor, sphere);
    ASSERT_NE(row, nullptr);

    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*sphere, NS::FindBaselineClassObj(*actor, *row), *radius));
}

TEST(ActorFile, TransformPartIsInstanceOnlyOnEveryPath)
{
    // 位置・回転・拡縮は配置物の物。.actor の読込・保存の差分・インスペクタの 3 つの道が同じ判断を通る
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

    const NS::FieldDesc* field = NS::FindField(baseline.Root().GetReflection(), NS::k_PositionFieldName);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(NS::FieldDestinationOf(baseline, baseline.Root(), *field), NS::FieldDestination::Placement);
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

// 書く途中で失敗しても前の .actor を壊さない。壊れると、それを指す配置物が全部組めなくなる
TEST(ActorFile, SaveThatCannotFinishLeavesTheFileAsItWas)
{
    using NS::OS::FileSystem;
    const ScopedActorFileDirectory scope("SaveKeepsOldFile");
    const std::string original = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)).dump();
    WriteActorFile(scope.Path(), "HeavyRock", original);
    // 書く途中の失敗を、一時のファイルの置き場をディレクトリで塞いで作る
    const std::string blocker = FileSystem::Combine(scope.Path(), "HeavyRock.actor.tmp");
    ASSERT_TRUE(FileSystem::CreateDirectories(blocker));

    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Set("HeavyRock", ActorFileWith("PropActor", "Collision", SphereEntry(5.0f)));
    {
        const TestLogCapture log;
        EXPECT_FALSE(library.Save("HeavyRock"));
    }
    const std::optional<std::string> text = FileSystem::ReadAllText(FileSystem::Combine(scope.Path(), "HeavyRock.actor"));
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, original);

    std::error_code error;
    std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(blocker)}, error);
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

namespace
{
    // 置物 2 体のシーン。.actor の半径は 2
    void ResetTwoProps(NS::Scene& scene)
    {
        NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));
        nlohmann::json doc = NS::MakeSceneJson();
        NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
        NS::SceneJsonObjects(doc).push_back(MapObjJson(2));
        scene.ResetFromJson(doc);
    }

    // インスペクタと同じ順で、actor の part の欄 fieldName を直し終える。値は呼ぶ前に part へ書いておく
    void EditField(LevelEditorController& editor,
                   const NS::SceneActor& actor,
                   const NS::Object* part,
                   std::string_view fieldName)
    {
        const NS::ClassObjInfo* row = RowOf(actor, part);
        ASSERT_NE(row, nullptr);
        editor.SelectObjectById(actor.Id());
        editor.BeginClassObjEdit();
        editor.NoteFieldEdit(actor, *row, fieldName);
        editor.CommitClassObjEdit();
    }

    // Directory() に書かれた PropActor.actor の Collision の半径。読めなければ -1
    float SavedRadius(const std::string& directory)
    {
        const std::optional<std::string> text =
            NS::OS::FileSystem::ReadAllText(NS::OS::FileSystem::Combine(directory, "PropActor.actor"));
        if (!text.has_value())
        {
            return -1.0f;
        }
        const nlohmann::json written = nlohmann::json::parse(*text);
        const nlohmann::json* fields = NS::ClassObjFields(written, "Collision");
        if (fields == nullptr)
        {
            return -1.0f;
        }
        return NS::FieldFloat(*fields, "半径", -1.0f);
    }
} // namespace

// 印の無い欄を直し終えると .actor のファイルへ書き、同じ .actor のアクターの全部に効く。戻すと全部が前の値へ戻る
TEST(ActorFile, EditorWritesAnUnmarkedFieldToTheActorFileForEveryActor)
{
    const ScopedActorFileDirectory scope("EditorWritesActorFile");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    NS::SceneActor* first = scene.Objects().FindByObjectId(1);
    ASSERT_NE(first, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(first->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, *first, sphere, "半径"));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 3.0f);

    NS::Editor::ObjectSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 2.0f);

    ASSERT_TRUE(editor.Editor().Undo().Redo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 3.0f);
}

// .actor の無いアクターの欄を直して戻すと、.actor は直す前と同じく無い。class だけの .actor を残さない
TEST(ActorFile, UndoingTheFirstEditOfAnActorWithoutAFileLeavesNoFile)
{
    using NS::OS::FileSystem;
    const ScopedActorFileDirectory scope("UndoFirstActorFile");
    NS::Scene scene;
    const NS::uint32 id = scene.SpawnObject<GL::Level::PropActor>("Rock")->Id();
    ASSERT_EQ(NS::ActorFileLibrary::Get().Find("PropActor"), nullptr);
    LevelEditorController editor(&scene);
    NS::SceneActor* rock = scene.Objects().FindByObjectId(id);
    ASSERT_NE(rock, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(rock->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    const float codeDefault = sphere->Radius();

    sphere->SetRadius(codeDefault + 1.0f);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, *rock, sphere, "半径"));
    const std::string path = FileSystem::Combine(scope.Path(), "PropActor.actor");
    ASSERT_TRUE(FileSystem::Exists(path));

    NS::Editor::ObjectSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_EQ(NS::ActorFileLibrary::Get().Find("PropActor"), nullptr);
    EXPECT_FALSE(FileSystem::Exists(path));
    EXPECT_FLOAT_EQ(RadiusOf(*rock), codeDefault);

    ASSERT_TRUE(editor.Editor().Undo().Redo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), codeDefault + 1.0f);
}

// 印の付いた欄を直すと、その 1 体だけが変わり、.actor は変わらない
TEST(ActorFile, EditorWritesAMarkedFieldToThatActorOnly)
{
    const ScopedActorFileDirectory scope("EditorWritesPlacement");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    GL::Level::PropActor* first = NS::Cast<GL::Level::PropActor>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(first, nullptr);
    const nlohmann::json before = *NS::ActorFileLibrary::Get().Find("PropActor");
    const float otherMass = MassOf(*scene.Objects().FindByObjectId(2));

    ASSERT_EQ(NS::ApplyJsonFields(first->Params(), MassEntry(otherMass + 3.0f)), 0u);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, *first, &first->Params(), "質量"));
    EXPECT_EQ(*NS::ActorFileLibrary::Get().Find("PropActor"), before);
    EXPECT_FALSE(NS::OS::FileSystem::Exists(NS::OS::FileSystem::Combine(scope.Path(), "PropActor.actor")));
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(2)), otherMass);
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(1)), otherMass + 3.0f);
}

// プレイ中に印の無い欄を直しても .actor に書き、プレイ開始の凍結は変えない。編集へ戻っても値が残り、WARN は出ない
TEST(ActorFile, PlayEditOfAnUnmarkedFieldGoesToTheActorFileAndSurvivesReturningToEdit)
{
    const ScopedActorFileDirectory scope("PlayEditActorFile");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    const nlohmann::json frozen = scene.PlayBaseline();
    NS::SceneActor* first = scene.Objects().FindByObjectId(1);
    ASSERT_NE(first, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(first->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, *first, sphere, "半径"));
    // 凍結へ写す口に直に渡しても、.actor へ書く欄は写さない
    const NS::ClassObjInfo* row = RowOf(*first, sphere);
    ASSERT_NE(row, nullptr);
    scene.WritePlayBaselineField(*first, *row, "半径");
    EXPECT_EQ(scene.PlayBaseline(), frozen);
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 3.0f);

    const TestLogCapture log;
    editor.EnterEdit();
    EXPECT_EQ(log.WarningCount(), 0);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 3.0f);
}

namespace
{
    // 素材の窓で選んだ .actor の 1 体。選べなければ nullptr
    NS::SceneActor* SelectActorFile(LevelEditorController& editor, const std::string& directory, std::string_view name)
    {
        editor.SelectAsset(NS::OS::FileSystem::Combine(directory, std::string{name} + ".actor"));
        return NS::Cast<NS::SceneActor>(editor.SelectedAsset());
    }

    // インスペクタと同じ順で、素材の窓で選んだ .actor の part の欄 fieldName を直し終える。値は呼ぶ前に part へ書いておく
    void EditActorFileField(LevelEditorController& editor,
                            const NS::SceneActor& actorFile,
                            const NS::Object* part,
                            std::string_view fieldName)
    {
        const NS::ClassObjInfo* row = RowOf(actorFile, part);
        ASSERT_NE(row, nullptr);
        editor.NoteActorFileFieldEdit(*row, fieldName);
        editor.CommitAssetEdit();
    }
} // namespace

// 素材の窓で選んだ .actor の欄を直すと .actor に書かれ、シーンの同じ .actor のアクターの全部に効く
TEST(ActorFile, ActorFileSelectedInTheAssetWindowIsEditedForEveryActor)
{
    const ScopedActorFileDirectory scope("AssetWindowActorFile");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    NS::SceneActor* selected = SelectActorFile(editor, scope.Path(), "PropActor");
    ASSERT_NE(selected, nullptr);
    EXPECT_FALSE(editor.SelectedAssetPath().empty());
    EXPECT_EQ(selected->ActorFileName(), "PropActor");
    EXPECT_FLOAT_EQ(RadiusOf(*selected), 2.0f);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(selected->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditActorFileField(editor, *selected, sphere, "半径"));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 3.0f);

    // 戻すと .actor とアクターの全部が前の値へ戻り、選んだ .actor も前の値を見せる
    NS::Editor::ObjectSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(1)), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(*scene.Objects().FindByObjectId(2)), 2.0f);
    NS::SceneActor* again = NS::Cast<NS::SceneActor>(editor.SelectedAsset());
    ASSERT_NE(again, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(*again), 2.0f);
}

// 素材の窓では印の付いた欄の .actor の値も直せる。上書きの無いアクターは付いて来て、上書きのあるアクターは残る
TEST(ActorFile, MarkedFieldEditedOnTheActorFileReachesActorsWithoutAnOverride)
{
    const ScopedActorFileDirectory scope("AssetWindowMarkedField");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Params", MassEntry(2.0f)));
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(MapObjJson(1));
    nlohmann::json overridden = MapObjJson(2);
    NS::ObjectJsonClassObjs(overridden)["Params"] = MassEntry(5.0f);
    NS::SceneJsonObjects(doc).push_back(std::move(overridden));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);
    GL::Level::PropActor* selected = NS::Cast<GL::Level::PropActor>(SelectActorFile(editor, scope.Path(), "PropActor"));
    ASSERT_NE(selected, nullptr);

    ASSERT_EQ(NS::ApplyJsonFields(selected->Params(), MassEntry(4.0f)), 0u);
    ASSERT_NO_FATAL_FAILURE(EditActorFileField(editor, *selected, &selected->Params(), "質量"));
    const nlohmann::json* written = NS::ActorFileLibrary::Get().Find("PropActor");
    ASSERT_NE(written, nullptr);
    const nlohmann::json* params = NS::ClassObjFields(*written, "Params");
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*params, "質量", 0.0f), 4.0f);
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(1)), 4.0f);
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(2)), 5.0f);
    // 付いて来たアクターは上書きを持たないまま保存される
    const nlohmann::json saved = scene.ToJson();
    for (const nlohmann::json& object : NS::SceneJsonObjects(saved))
    {
        if (NS::ObjectJsonId(object) != 1u)
        {
            continue;
        }
        const nlohmann::json* savedParams = SavedFields(object, "Params");
        EXPECT_TRUE(savedParams == nullptr || !savedParams->contains("質量"));
    }

    NS::Editor::ObjectSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(1)), 2.0f);
    EXPECT_FLOAT_EQ(MassOf(*scene.Objects().FindByObjectId(2)), 5.0f);
}

TEST(ActorFile, ShippedActorFilesLoad)
{
    // 出荷の .actor はどれも登録済みのクラスの物で、置物はクラスが影を足す
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

// Set は Reload と同じ決まりで断る。入れると Save で書いた .actor を次の起動で読めない
TEST(ActorFile, SetRefusesWhatReloadWouldNotRead)
{
    using NS::OS::FileSystem;
    const ScopedActorFileDirectory scope("SetRefuses");
    std::map<std::string, nlohmann::json> refused;
    refused["UnknownKey"] = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f));
    refused["UnknownKey"]["hitTimeline"] = nlohmann::json{{"Hit", 3}};
    refused["NoClass"] = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f));
    refused["NoClass"].erase("class");
    refused["Unregistered"] = ActorFileWith("NoSuchActorClass", "Collision", SphereEntry(2.0f));

    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    const TestLogCapture log;
    for (const std::pair<const std::string, nlohmann::json>& entry : refused)
    {
        library.Set(entry.first, entry.second);
        EXPECT_EQ(library.Find(entry.first), nullptr) << entry.first;
        EXPECT_FALSE(library.Save(entry.first)) << entry.first;
        EXPECT_FALSE(FileSystem::Exists(FileSystem::Combine(scope.Path(), entry.first + ".actor"))) << entry.first;
    }
    EXPECT_EQ(log.ErrorCount(), 3);
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
