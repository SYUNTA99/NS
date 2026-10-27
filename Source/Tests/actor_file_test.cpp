#include "Editor/ActorFileGenerator.h"
#include "Editor/EditorActors.h"
#include "Editor/EditorAppearance.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/StringUtils.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"
#include "TestRegistrations.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// 値の 3 段の重なり (コードの既定値 < .actor の値 < シーンのアクターの上書きできる欄) と、差分の保存と、エディタの書く先を縛る

namespace
{
    // 全部の項目をコードの値で書いた .actor の文書に、部品 role の項目 entry を重ねる
    nlohmann::json ActorFileWith(std::string_view className, std::string_view role, const nlohmann::json& entry)
    {
        nlohmann::json actorFile = CodeValuesActorFile(className);
        nlohmann::json& fields = NS::ActorJsonClassObjs(actorFile)[std::string{role}];
        if (!fields.is_object())
        {
            fields = nlohmann::json::object();
        }
        for (nlohmann::json::const_iterator field = entry.begin(); field != entry.end(); ++field)
        {
            fields[field.key()] = field.value();
        }
        return actorFile;
    }

    nlohmann::json SphereEntry(float radius)
    {
        return nlohmann::json{{"半径", radius}};
    }

    // 置物の質量の欄。元と派生の .actor に書けるプロテクトの項目
    nlohmann::json MassEntry(float mass)
    {
        return nlohmann::json{{"質量", mass}};
    }

    // 影の基本直径の欄。シーンで上書きできるグローバルの項目
    nlohmann::json DiameterEntry(float diameter)
    {
        return nlohmann::json{{"基本直径", diameter}};
    }

    // actor の影の基本直径。影が無ければ -1
    float DiameterOf(const NS::SceneActor* actor)
    {
        NS_ASSERT(Game, actor != nullptr, "DiameterOf: 渡したアクターか部品が無い");
        const NS::Shadow* shadow = actor->ShadowClassObj();
        if (shadow == nullptr)
        {
            return -1.0f;
        }
        return NS::FieldFloat(NS::SerializeClassObjFields(shadow), "基本直径", -1.0f);
    }

    float MassOf(const NS::SceneActor* actor)
    {
        NS_ASSERT(Game, actor != nullptr, "MassOf: 渡したアクターか部品が無い");
        const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor);
        if (prop == nullptr)
        {
            return -1.0f;
        }
        return prop->Params().Mass();
    }

    nlohmann::json MapObjJson(NS::uint32 id)
    {
        nlohmann::json object = NS::MakeActorJson();
        NS::SetActorJsonFile(object, "PropActor");
        NS::SetActorJsonId(object, id);
        return object;
    }

    float RadiusOf(const NS::SceneActor* actor)
    {
        NS_ASSERT(Game, actor != nullptr, "RadiusOf: 渡したアクターか部品が無い");
        const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
        if (sphere == nullptr)
        {
            return -1.0f;
        }
        return sphere->Radius();
    }

    // actor の表で part を持つ行。無ければ nullptr
    const NS::ClassObjInfo* RowOf(const NS::SceneActor* actor, const NS::Object* part)
    {
        NS_ASSERT(Game, actor != nullptr, "RowOf: 渡したアクターか部品が無い");
        const NS::ClassObjInfo* row = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& field, const NS::Object* candidate) {
            if (candidate == part)
            {
                row = &field;
            }
        });
        return row;
    }

    // 保存したシーンのアクターの JSON から、部品の件の fields を引く。無ければ nullptr
    const nlohmann::json* SavedFields(const nlohmann::json& object, std::string_view typeName)
    {
        return NS::ClassObjFields(object, typeName);
    }
} // namespace

TEST(ActorFile, CodeDefaultWhenTheActorFileHasNoValues)
{
    const ScopedActorFileDirectory scope("CodeDefault");
    const ScopedActorFile classOnly{"PropActor", CodeValuesActorFile("PropActor")};
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(actor.get()), 0.5f);
    EXPECT_NE(actor->ShadowClassObj(), nullptr);
}

TEST(ActorFile, ActorFileOverridesCodeDefault)
{
    const ScopedActorFileDirectory scope("OverridesCode");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(actor.get()), 2.0f);
}

TEST(ActorFile, InstanceOverridesActorFile)
{
    const ScopedActorFileDirectory scope("InstanceOverrides");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Shadow", DiameterEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::ActorJsonClassObjs(object)["Shadow"] = DiameterEntry(3.0f);
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(DiameterOf(actor.get()), 3.0f);
}

TEST(ActorFile, ActorFileCannotAddParts)
{
    const ScopedActorFileDirectory scope("FixedParts");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "AttackSensor", nlohmann::json::object()));

    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->AttackSensorClassObj(), nullptr);
    EXPECT_NE(actor->ShadowClassObj(), nullptr);
}

// シーンの上書きは部品を足せない。クラスに無い部品を書いたアクターは作らない
TEST(ActorFile, InstanceCannotAddParts)
{
    const ScopedActorFileDirectory scope("InstanceParts");
    nlohmann::json object = MapObjJson(1);
    NS::ActorJsonClassObjs(object)["DirectionalLight"] = nlohmann::json::object();

    NS::Scene scene;
    const TestLogCapture log;
    EXPECT_EQ(NS::ActorFromJson(object, scene), nullptr);
    EXPECT_EQ(log.ErrorCount(), 1);
}

// シーンの上書きで断ったアクターは、部品をシーンの管理クラスへ 1 つも残さない。
// 残すと、捨てたアクターの部品を管理クラスが指したまま次の描画と当たりの判定で使う
TEST(ActorFile, RejectedPlacementLeavesNoRegistration)
{
    const ScopedActorFileDirectory scope("RejectedRegistration");
    nlohmann::json refused = MapObjJson(1);
    NS::ActorJsonClassObjs(refused)["Collision"] = SphereEntry(3.0f);

    NS::Scene scene;
    const Registrations before = CountRegistrations(scene);
    {
        const TestLogCapture log;
        EXPECT_EQ(NS::ActorFromJson(refused, scene), nullptr);
        EXPECT_GE(log.ErrorCount(), 1);
    }
    EXPECT_EQ(CountRegistrations(scene), before);
}

TEST(ActorFile, ActorWithoutActorFileIsNotBuilt)
{
    nlohmann::json object = NS::MakeActorJson();
    NS::ActorJsonClassObjs(object)["Collision"] = SphereEntry(1.0f);
    NS::Scene scene;
    EXPECT_EQ(NS::ActorFromJson(object, scene), nullptr);
}

// 型から湧かすアクターにもクラスと同じ名前の .actor が届く。作る道で型の値が変わらない
TEST(ActorFile, SpawnedByTypeGetsActorFile)
{
    const ScopedActorFileDirectory scope("SpawnedByType");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    NS::Scene scene;
    const NS::SceneActor* actor = scene.SpawnActor<GL::Level::PropActor>("PropActor");
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(actor), 2.0f);
}

TEST(ActorFile, SaveWritesOnlyOverrides)
{
    const ScopedActorFileDirectory scope("SaveOverrides");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Shadow", DiameterEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(MapObjJson(1));
    NS::SceneJsonActors(doc).push_back(MapObjJson(2));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    GL::Level::PropActor* changed = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
    ASSERT_NE(changed, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(changed->ShadowClassObj(), DiameterEntry(4.0f)), 0u);
    changed->AddSceneOverride("Shadow", "基本直径");

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonActors(saved);
    const nlohmann::json& plain = objects[NS::FindActorIndexById(saved, 1)];
    const nlohmann::json& overridden = objects[NS::FindActorIndexById(saved, 2)];

    // 欄が全部既定と同じ部品も空の件として残り、.actor の値と同じ欄は書かれない
    const nlohmann::json* plainShadow = NS::ClassObjFields(plain, "Shadow");
    ASSERT_NE(plainShadow, nullptr);
    EXPECT_FALSE(plainShadow->contains("id"));
    EXPECT_FALSE(NS::HasField(*plainShadow, "基本直径"));
    // 上書きした欄だけが書かれる
    const nlohmann::json* overriddenShadow = NS::ClassObjFields(overridden, "Shadow");
    ASSERT_NE(overriddenShadow, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*overriddenShadow, "基本直径", 0.0f), 4.0f);
    // 姿勢もほかのグローバルの項目と同じく、上書きしていなければ書かない
    const nlohmann::json* transform = SavedFields(plain, "Transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_FALSE(transform->contains(NS::k_PositionFieldName));
}

TEST(ActorFile, ChangedActorFileReachesInstancesWithoutOverride)
{
    const ScopedActorFileDirectory scope("ReachesInstances");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Shadow", DiameterEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(MapObjJson(1));
    nlohmann::json overridden = MapObjJson(2);
    NS::ActorJsonClassObjs(overridden)["Shadow"] = DiameterEntry(4.0f);
    NS::SceneJsonActors(doc).push_back(std::move(overridden));

    // .actor を変えてから読むと、上書きの無いアクターだけが新しい値になる
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Shadow", DiameterEntry(6.0f)));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(1)), 6.0f);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(2)), 4.0f);
}

TEST(ActorFile, ApplyingSnapshotResetsFieldsWithoutOverride)
{
    // undo の控えは上書きだけを持つ。控えを当てると、控えに無い欄も .actor の値へ戻る
    const ScopedActorFileDirectory scope("SnapshotReset");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f)));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(MapObjJson(1));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Actors().FindByActorId(1);
    ASSERT_NE(actor, nullptr);
    const nlohmann::json snapshot = NS::ActorToJson(actor);

    NS::Cast<NS::SphereCollision>(actor->CollisionClassObj())->SetRadius(9.0f);
    NS::SceneActor* applied = ApplySnapshot(scene, snapshot);
    ASSERT_EQ(applied, actor); // 同じクラスなので作り直さない
    EXPECT_FLOAT_EQ(RadiusOf(actor), 2.0f);
}

// 保存は値を比べず、上書きの一覧の項目を書く。.actor と同じ値の上書きも残り、書いていない項目は書かない
TEST(ActorFile, OverrideWithTheActorFileValueIsKeptOnSave)
{
    const ScopedActorFileDirectory scope("OverrideKept");
    ASSERT_TRUE(
        NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Shadow", DiameterEntry(2.0f))));

    nlohmann::json object = MapObjJson(7);
    NS::ActorJsonClassObjs(object)["Shadow"] = DiameterEntry(2.0f);
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_TRUE(actor->IsSceneOverride("Shadow", "基本直径"));
    EXPECT_FALSE(actor->IsSceneOverride("Model", "基本色"));

    const nlohmann::json saved = NS::ActorToJson(actor.get());
    const nlohmann::json* shadow = NS::ClassObjFields(saved, "Shadow");
    ASSERT_NE(shadow, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*shadow, "基本直径", 0.0f), 2.0f);
    EXPECT_FALSE(NS::HasField(*shadow, "最大投影距離"));
    const nlohmann::json* model = NS::ClassObjFields(saved, "Model");
    EXPECT_TRUE(model == nullptr || !NS::HasField(*model, "基本色"));
}

// 一覧に無い項目は、アクターの値が .actor と違っても保存で書かない
TEST(ActorFile, FieldOutsideTheOverridesIsNotSavedEvenWhenItDiffers)
{
    const ScopedActorFileDirectory scope("NotListedNotSaved");
    const ScopedActorFile heavy{"HeavyRock", ActorFileWith("PropActor", "Params", MassEntry(3.0f))};
    nlohmann::json object = MapObjJson(1);
    NS::SetActorJsonFile(object, "HeavyRock");
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(MassOf(actor.get()), 3.0f);

    GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor.get());
    ASSERT_NE(prop, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&prop->Params(), MassEntry(5.0f)), 0u);
    const nlohmann::json saved = NS::ActorToJson(actor.get());
    const nlohmann::json* params = NS::ClassObjFields(saved, "Params");
    ASSERT_NE(params, nullptr);
    EXPECT_FALSE(NS::HasField(*params, "質量"));
}

namespace
{
    // info の欄 name の項目の種類が scope か。欄が無ければ偽
    bool HasScope(const NS::ReflectionInfo* info, std::string_view name, NS::FieldScope scope)
    {
        const NS::FieldDesc* field = NS::FindField(info, name);
        return field != nullptr && field->scope == scope;
    }

    // info の欄のうち、項目の種類が scope の物の数
    int ScopeCount(const NS::ReflectionInfo* info, NS::FieldScope scope)
    {
        int count = 0;
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (info->fields[i].scope == scope)
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

// グローバルの項目は、置き場所ごとに変える見た目の 3 つと、地形の形を置き場所ごとに分けるメッシュの欄。
// 置物の質量と耐久は種類の性質なので、派生ごとに揃えるプロテクトの項目
TEST(ActorFile, GlobalFieldsAreTheFourLooksAndMassAndToughnessAreProtected)
{
    const NS::ReflectionInfo* params = GL::Level::MapObjParams::StaticReflection();
    EXPECT_TRUE(HasScope(params, "耐久", NS::FieldScope::Protected));
    EXPECT_TRUE(HasScope(params, "質量", NS::FieldScope::Protected));
    EXPECT_EQ(ScopeCount(params, NS::FieldScope::Protected), 2);
    EXPECT_EQ(ScopeCount(params, NS::FieldScope::Global), 0);

    const NS::ReflectionInfo* model = NS::Model::StaticReflection();
    EXPECT_TRUE(HasScope(model, "基本色", NS::FieldScope::Global));
    EXPECT_TRUE(HasScope(model, "マテリアル", NS::FieldScope::Global));
    EXPECT_TRUE(HasScope(model, "メッシュ", NS::FieldScope::Global));
    EXPECT_EQ(ScopeCount(model, NS::FieldScope::Global), 3);

    const NS::ReflectionInfo* shadow = NS::Shadow::StaticReflection();
    EXPECT_TRUE(HasScope(shadow, "基本直径", NS::FieldScope::Global));
    EXPECT_EQ(ScopeCount(shadow, NS::FieldScope::Global), 1);

    EXPECT_TRUE(HasScope(NS::SphereCollision::StaticReflection(), "半径", NS::FieldScope::Shared));
}

// シーンの上書きに書いてよくない項目があれば、アクターを作らずに ERROR を出し、JSON のまま持って保存で書き戻す
TEST(ActorFile, PlacementWithAFieldTheSceneCannotWriteIsKeptAsJson)
{
    const ScopedActorFileDirectory scope("PlacementRefused");
    ASSERT_TRUE(NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f))));

    nlohmann::json allowed = MapObjJson(1);
    NS::ActorJsonClassObjs(allowed)["Shadow"] = DiameterEntry(8.0f);
    nlohmann::json refused = MapObjJson(2);
    NS::ActorJsonClassObjs(refused)["Collision"] = SphereEntry(3.0f);
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(allowed);
    NS::SceneJsonActors(doc).push_back(refused);
    NS::SetSceneJsonNextActorId(doc, 3);

    NS::Scene scene;
    {
        const TestLogCapture log;
        scene.ResetFromJson(doc);
        EXPECT_GE(log.ErrorCount(), 1);
    }
    GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(1));
    ASSERT_NE(prop, nullptr);
    EXPECT_FLOAT_EQ(DiameterOf(prop), 8.0f);
    EXPECT_FLOAT_EQ(RadiusOf(prop), 2.0f);
    EXPECT_EQ(scene.Actors().FindByActorId(2), nullptr);

    const nlohmann::json savedScene = scene.ToJson();
    const nlohmann::json& objects = NS::SceneJsonActors(savedScene);
    ASSERT_EQ(objects.size(), 2u);
    bool keptRefused = false;
    for (const nlohmann::json& saved : objects)
    {
        if (NS::ActorJsonId(saved) == 2u)
        {
            keptRefused = true;
            const nlohmann::json* sphere = NS::ClassObjFields(saved, "Collision");
            ASSERT_NE(sphere, nullptr);
            EXPECT_FLOAT_EQ(NS::FieldFloat(*sphere, "半径", 0.0f), 3.0f);
        }
    }
    EXPECT_TRUE(keptRefused);
}

// 参照の欄は上書きできる項目でなくてもシーンのアクターから読み書きされ、シーンに書いた位置は上書きとして読み書きされる
TEST(ActorFile, ReferenceAndPoseFieldsComeFromThePlacement)
{
    const ScopedActorFileDirectory scope("PlacementRefAndRoot");
    const ScopedActorFile classOnly{"FollowCameraActor", CodeValuesActorFile("FollowCameraActor")};

    nlohmann::json object = NS::MakeActorJson();
    NS::SetActorJsonFile(object, "FollowCameraActor");
    NS::SetActorJsonId(object, 2);
    nlohmann::json follow = nlohmann::json::object();
    NS::SetField(follow, "追従対象", NS::ActorRef{1});
    NS::ActorJsonClassObjs(object)["Vcam"] = std::move(follow);
    NS::Editor::SetActorJsonPosition(object, NS::Vector3{1.0f, 2.0f, 3.0f});

    const TestLogCapture log;
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(object, scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(log.WarningCount(), 0);
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(actor.get());
    ASSERT_NE(camera, nullptr);
    EXPECT_EQ(camera->Vcam().TargetRef(), NS::ActorRef{1});
    EXPECT_FLOAT_EQ(actor->Root().Position().y, 2.0f);

    const nlohmann::json saved = NS::ActorToJson(actor.get());
    const nlohmann::json* savedVcam = NS::ClassObjFields(saved, "Vcam");
    ASSERT_NE(savedVcam, nullptr);
    EXPECT_TRUE(NS::HasField(*savedVcam, "追従対象"));
    EXPECT_FLOAT_EQ(NS::Editor::ActorJsonPosition(saved).z, 3.0f);
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

    // インスペクタで直しても .actor へは書かず、シーンのアクターへ書く
    const std::unique_ptr<NS::SceneActor> baselineOwned = ActorFromFile("FollowCameraActor");
    ASSERT_NE(baselineOwned, nullptr);
    const NS::SceneActor& baseline = *baselineOwned;
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(&baseline);
    ASSERT_NE(camera, nullptr);
    const NS::FieldDesc* target = NS::FindField(camera->Vcam().GetReflection(), "追従対象");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(NS::FieldDestinationOf(camera, *target), NS::FieldDestination::Scene);
}

// 字の色の上書きの表示は、シーンへ書く欄が上書きの一覧にある時だけ付く。値が .actor と違うだけでは付かない。
// .actor へ書く欄には付かない
TEST(ActorFile, OverrideIsShownOnlyOnFieldsInTheSceneOverrides)
{
    const ScopedActorFileDirectory scope("OverrideDetect");
    nlohmann::json actorFile = ActorFileWith("PropActor", "Collision", SphereEntry(2.0f));
    NS::ActorJsonClassObjs(actorFile)["Shadow"] = DiameterEntry(2.0f);
    NS::ActorFileLibrary::Get().Set("PropActor", std::move(actorFile));

    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(MapObjJson(1));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(1));
    ASSERT_NE(prop, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(prop->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    const NS::ClassObjInfo* sphereRow = RowOf(prop, sphere);
    NS::Shadow* shadow = prop->ShadowClassObj();
    ASSERT_NE(shadow, nullptr);
    const NS::ClassObjInfo* shadowRow = RowOf(prop, shadow);
    ASSERT_NE(sphereRow, nullptr);
    ASSERT_NE(shadowRow, nullptr);
    const NS::FieldDesc* radius = NS::FindField(sphere->GetReflection(), "半径");
    const NS::FieldDesc* diameter = NS::FindField(shadow->GetReflection(), "基本直径");
    ASSERT_NE(radius, nullptr);
    ASSERT_NE(diameter, nullptr);

    EXPECT_FALSE(NS::Editor::ShowsOverride(shadow, shadowRow->key, *diameter, prop));
    ASSERT_EQ(NS::ApplyJsonFields(shadow, DiameterEntry(4.0f)), 0u);
    EXPECT_FALSE(NS::Editor::ShowsOverride(shadow, shadowRow->key, *diameter, prop));
    prop->AddSceneOverride(shadowRow->key, "基本直径");
    EXPECT_TRUE(NS::Editor::ShowsOverride(shadow, shadowRow->key, *diameter, prop));

    sphere->SetRadius(2.5f);
    prop->AddSceneOverride(sphereRow->key, "半径");
    EXPECT_FALSE(NS::Editor::ShowsOverride(sphere, sphereRow->key, *radius, prop));
}

// 欄の書く先は 1 つの関数が決め、行の地の色は同じ答えの方の色になる
TEST(ActorFile, EachFieldKindHasOneDestinationAndTheRowColorFollowsIt)
{
    const ScopedActorFileDirectory scope("FieldDestination");
    const ScopedActorFile propFile{"PropActor", CodeValuesActorFile("PropActor")};
    const ScopedActorFile cameraFile{"FollowCameraActor", CodeValuesActorFile("FollowCameraActor")};
    const std::unique_ptr<NS::SceneActor> propOwned = ActorFromFile("PropActor");
    ASSERT_NE(propOwned, nullptr);
    const NS::SceneActor& prop = *propOwned;
    const GL::Level::PropActor* rock = NS::Cast<GL::Level::PropActor>(&prop);
    const std::unique_ptr<NS::SceneActor> cameraActorOwned = ActorFromFile("FollowCameraActor");
    ASSERT_NE(cameraActorOwned, nullptr);
    const NS::SceneActor& cameraActor = *cameraActorOwned;
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
        {rock, &rock->Params(), "質量", NS::FieldDestination::ActorFile},
        {rock, rock->ShadowClassObj(), "基本直径", NS::FieldDestination::Scene},
        {camera, &camera->Vcam(), "追従対象", NS::FieldDestination::Scene},
        {rock, &rock->Root(), NS::k_PositionFieldName, NS::FieldDestination::Scene},
    };
    // シーンへ書く欄だけを塗り、.actor へ書く欄は塗らない
    NS::Editor::EditorAppearance appearance;
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.field);
        const NS::FieldDesc* field = NS::FindField(c.part->GetReflection(), c.field);
        ASSERT_NE(field, nullptr);
        const NS::FieldDestination destination = NS::FieldDestinationOf(c.actor, *field);
        EXPECT_EQ(destination, c.expected);
        const std::optional<NS::Color> color = NS::Editor::FieldRowColor(appearance, c.actor, *field);
        if (destination != NS::FieldDestination::Scene)
        {
            EXPECT_FALSE(color.has_value());
        }
        else
        {
            ASSERT_TRUE(color.has_value());
            EXPECT_EQ(*color, appearance.placementRowColor);
        }
    }
}

namespace
{
    // 項目の種類を 1 つずつ持つ部品
    class FieldScopeProbePart final : public NS::Object
    {
    public:
        float shared = 0.0f;
        float protectedValue = 0.0f;
        float global = 0.0f;
        NS_REFLECT_BEGIN(FieldScopeProbePart, NS::Object)
        NS_FIELD(shared, "共有")
        NS_PROTECTED_FIELD(protectedValue, "派生ごと")
        NS_GLOBAL_FIELD(global, "シーンごと")
        NS_REFLECT_END()
    };
} // namespace

// 登録のマクロが項目の種類を決め、種類と指す .actor が元か派生かで書く先が決まる
TEST(ActorFile, FieldScopeAndTheDerivedActorFileDecideTheDestination)
{
    const NS::ReflectionInfo* info = FieldScopeProbePart::StaticReflection();
    const NS::FieldDesc* shared = NS::FindField(info, "共有");
    const NS::FieldDesc* protectedField = NS::FindField(info, "派生ごと");
    const NS::FieldDesc* global = NS::FindField(info, "シーンごと");
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(protectedField, nullptr);
    ASSERT_NE(global, nullptr);
    EXPECT_EQ(shared->scope, NS::FieldScope::Shared);
    EXPECT_EQ(protectedField->scope, NS::FieldScope::Protected);
    EXPECT_EQ(global->scope, NS::FieldScope::Global);

    const ScopedActorFileDirectory scope("FieldScope");
    const ScopedActorFile originFile{"PropActor", CodeValuesActorFile("PropActor")};
    nlohmann::json derivedDocument = ClassOnlyActorFile("PropActor");
    NS::SetActorFileBase(derivedDocument, "PropActor");
    const ScopedActorFile derivedFile{"PropActorHeavy", derivedDocument};
    const std::unique_ptr<NS::SceneActor> originOwned = ActorFromFile("PropActor");
    ASSERT_NE(originOwned, nullptr);
    const NS::SceneActor& origin = *originOwned;
    const std::unique_ptr<NS::SceneActor> derivedOwned = ActorFromFile("PropActorHeavy");
    ASSERT_NE(derivedOwned, nullptr);
    const NS::SceneActor& derived = *derivedOwned;
    EXPECT_EQ(NS::ActorFileBase(*NS::ActorFileLibrary::Get().Find("PropActorHeavy")), "PropActor");

    EXPECT_EQ(NS::FieldDestinationOf(&origin, *shared), NS::FieldDestination::ActorFile);
    EXPECT_EQ(NS::FieldDestinationOf(&origin, *protectedField), NS::FieldDestination::ActorFile);
    EXPECT_EQ(NS::FieldDestinationOf(&origin, *global), NS::FieldDestination::Scene);
    // 派生を指すアクターでも、共有の項目は元へ書く
    EXPECT_EQ(NS::FieldDestinationOf(&derived, *shared), NS::FieldDestination::ActorFile);
    EXPECT_EQ(NS::FieldDestinationOf(&derived, *protectedField), NS::FieldDestination::DerivedActorFile);
    EXPECT_EQ(NS::FieldDestinationOf(&derived, *global), NS::FieldDestination::Scene);
}

// 上書きの種類ごとに書いてよい項目が決まる。参照の欄はシーンだけが書く
TEST(ActorFile, EachLayerCanWriteOnlyItsFieldKinds)
{
    const ScopedActorFileDirectory scope("CanWriteField");
    const ScopedActorFile propFile{"PropActor", CodeValuesActorFile("PropActor")};
    const ScopedActorFile cameraFile{"FollowCameraActor", CodeValuesActorFile("FollowCameraActor")};
    const std::unique_ptr<NS::SceneActor> rockOwned = ActorFromFile("PropActor");
    ASSERT_NE(rockOwned, nullptr);
    const NS::SceneActor& rock = *rockOwned;
    const std::unique_ptr<NS::SceneActor> cameraActorOwned = ActorFromFile("FollowCameraActor");
    ASSERT_NE(cameraActorOwned, nullptr);
    const NS::SceneActor& cameraActor = *cameraActorOwned;
    const GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(&cameraActor);
    ASSERT_NE(camera, nullptr);
    const NS::ReflectionInfo* info = FieldScopeProbePart::StaticReflection();

    struct Case
    {
        const NS::FieldDesc* field;
        bool actorFile;
        bool derived;
        bool scene;
    };
    const Case cases[] = {
        {NS::FindField(info, "共有"), true, false, false},
        {NS::FindField(info, "派生ごと"), true, true, false},
        {NS::FindField(info, "シーンごと"), true, true, true},
        {NS::FindField(rock.Root().GetReflection(), NS::k_PositionFieldName), true, true, true},
        {NS::FindField(camera->Vcam().GetReflection(), "追従対象"), false, false, true},
    };
    for (const Case& c : cases)
    {
        ASSERT_NE(c.field, nullptr);
        SCOPED_TRACE(c.field->name);
        EXPECT_EQ(NS::CanWriteField(NS::FieldDestination::ActorFile, *c.field), c.actorFile);
        EXPECT_EQ(NS::CanWriteField(NS::FieldDestination::DerivedActorFile, *c.field), c.derived);
        EXPECT_EQ(NS::CanWriteField(NS::FieldDestination::Scene, *c.field), c.scene);
    }
}

// 上書きに書いた項目だけが全部の値に重なり、書いてよくない項目・無い部品・無い欄があれば ERROR で重ねない
TEST(ActorFile, MergeLayerOverlaysWrittenFieldsAndRejectsFieldsTheLayerCannotWrite)
{
    const ScopedActorFileDirectory scope("MergeLayer");
    const ScopedActorFile propFile{"PropActor", CodeValuesActorFile("PropActor")};
    const std::unique_ptr<NS::SceneActor> rockOwned = ActorFromFile("PropActor");
    ASSERT_NE(rockOwned, nullptr);
    const NS::SceneActor& rock = *rockOwned;
    nlohmann::json base = nlohmann::json::object();
    NS::ActorJsonClassObjs(base) = {{"Shadow", {{"基本直径", 1.0f}, {"最大投影距離", 12.0f}}},
                                     {"Collision", {{"半径", 0.5f}}}};

    const auto layer = [](nlohmann::json classObjs) {
        nlohmann::json written = nlohmann::json::object();
        NS::ActorJsonClassObjs(written) = std::move(classObjs);
        return written;
    };

    {
        const TestLogCapture log;
        const std::optional<nlohmann::json> merged =
            NS::MergeLayer(base, layer({{"Shadow", {{"基本直径", 0.5f}}}}), NS::FieldDestination::Scene, &rock);
        ASSERT_TRUE(merged.has_value());
        EXPECT_EQ(log.ErrorCount(), 0);
        const nlohmann::json* shadow = NS::ClassObjFields(*merged, "Shadow");
        ASSERT_NE(shadow, nullptr);
        EXPECT_FLOAT_EQ(shadow->at("基本直径").get<float>(), 0.5f);
        EXPECT_FLOAT_EQ(shadow->at("最大投影距離").get<float>(), 12.0f);
        EXPECT_EQ(*NS::ClassObjFields(*merged, "Collision"), *NS::ClassObjFields(base, "Collision"));
    }
    {
        // 共有の項目は元の .actor には書けるが、シーンには書けない
        const nlohmann::json radius = layer({{"Collision", {{"半径", 2.0f}}}});
        const TestLogCapture log;
        EXPECT_TRUE(NS::MergeLayer(base, radius, NS::FieldDestination::ActorFile, &rock).has_value());
        EXPECT_FALSE(NS::MergeLayer(base, radius, NS::FieldDestination::Scene, &rock).has_value());
        EXPECT_EQ(log.ErrorCount(), 1);
    }
    {
        const TestLogCapture log;
        EXPECT_FALSE(NS::MergeLayer(base, layer({{"無い部品", {{"基本直径", 0.5f}}}}), NS::FieldDestination::Scene, &rock)
                         .has_value());
        EXPECT_FALSE(NS::MergeLayer(base, layer({{"Params", {{"無い欄", 0.5f}}}}), NS::FieldDestination::Scene, &rock)
                         .has_value());
        EXPECT_EQ(log.ErrorCount(), 2);
    }
}

// 「既定に戻す」は上書きの一覧から項目を外し、.actor の全部の値の同じ項目 (影の直径 2) を写す。保存にも書かない
TEST(ActorFile, RevertingASceneOverrideRemovesItAndCopiesTheActorFileValue)
{
    const ScopedActorFileDirectory scope("RevertSceneOverride");
    nlohmann::json actorFile = ActorFileWith("PropActor", "Shadow", DiameterEntry(2.0f));
    NS::ActorFileLibrary::Get().Set("PropActor", std::move(actorFile));

    nlohmann::json object = MapObjJson(1);
    NS::ActorJsonClassObjs(object)["Shadow"] = DiameterEntry(4.0f);
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(std::move(object));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* prop = scene.Actors().FindByActorId(1);
    ASSERT_NE(prop, nullptr);
    const NS::ClassObjInfo* row = RowOf(prop, prop->ShadowClassObj());
    ASSERT_NE(row, nullptr);
    ASSERT_TRUE(prop->IsSceneOverride(row->key, "基本直径"));
    ASSERT_FLOAT_EQ(DiameterOf(prop), 4.0f);

    EXPECT_TRUE(NS::RevertSceneOverride(prop, *row, "基本直径"));
    EXPECT_FALSE(prop->IsSceneOverride(row->key, "基本直径"));
    EXPECT_FLOAT_EQ(DiameterOf(prop), 2.0f);
    const nlohmann::json saved = NS::ActorToJson(prop);
    const nlohmann::json* fields = NS::ClassObjFields(saved, "Shadow");
    EXPECT_TRUE(fields == nullptr || !fields->contains("基本直径"));
}

// プレイ中に戻すと、プレイ開始の凍結からもその項目を消す。編集へ戻っても上書きは戻らない
TEST(ActorFile, RevertingDuringPlayRemovesTheFieldFromThePlayBaseline)
{
    const ScopedActorFileDirectory scope("RevertDuringPlay");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Shadow", DiameterEntry(2.0f)));

    nlohmann::json object = MapObjJson(1);
    NS::ActorJsonClassObjs(object)["Shadow"] = DiameterEntry(4.0f);
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(std::move(object));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    NS::SceneActor* prop = scene.Actors().FindByActorId(1);
    ASSERT_NE(prop, nullptr);
    const NS::ClassObjInfo* row = RowOf(prop, prop->ShadowClassObj());
    ASSERT_NE(row, nullptr);

    ASSERT_TRUE(NS::RevertSceneOverride(prop, *row, "基本直径"));
    editor.MirrorPlayEditToBaseline(prop, *row, "基本直径");
    editor.EnterEdit();

    prop = scene.Actors().FindByActorId(1);
    ASSERT_NE(prop, nullptr);
    EXPECT_FALSE(prop->IsSceneOverride(row->key, "基本直径"));
    EXPECT_FLOAT_EQ(DiameterOf(prop), 2.0f);
}

// 姿勢の位置・回転・スケールは、元と派生の .actor とシーンのどれにも書けるグローバルの項目
TEST(ActorFile, PoseFieldsAreGlobalItems)
{
    const NS::ReflectionInfo* info = NS::Transform::StaticReflection();
    for (const char* name : {NS::k_PositionFieldName, NS::k_RotationFieldName, NS::k_ScaleFieldName})
    {
        SCOPED_TRACE(name);
        const NS::FieldDesc* field = NS::FindField(info, name);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->scope, NS::FieldScope::Global);
    }
}

// 元の .actor のスケールを変えると、シーンでスケールを上書きしていないアクターに届き、上書きしたアクターには届かない
TEST(ActorFile, ActorFileScaleReachesOnlyActorsThatDoNotOverrideIt)
{
    const ScopedActorFileDirectory scope("PoseFromActorFile");
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    ASSERT_TRUE(library.Set("PropActor", CodeValuesActorFile("PropActor")));
    nlohmann::json overridden = MapObjJson(1);
    NS::Editor::SetActorJsonScale(overridden, NS::Vector3{3.0f, 3.0f, 3.0f});
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(std::move(overridden));
    NS::SceneJsonActors(doc).push_back(MapObjJson(2));
    NS::Scene scene;
    scene.ResetFromJson(doc);

    nlohmann::json scale = nlohmann::json::object();
    NS::SetField(scale, NS::k_ScaleFieldName, NS::Vector3{2.0f, 2.0f, 2.0f});
    ASSERT_TRUE(library.Set("PropActor", ActorFileWith("PropActor", "Transform", scale)));
    for (NS::SceneActor* actor : scene.Actors())
    {
        NS::ApplyActorFile(actor);
    }

    const NS::SceneActor* kept = scene.Actors().FindByActorId(1);
    const NS::SceneActor* reached = scene.Actors().FindByActorId(2);
    ASSERT_NE(kept, nullptr);
    ASSERT_NE(reached, nullptr);
    EXPECT_EQ(kept->Root().Scale(), (NS::Vector3{3.0f, 3.0f, 3.0f}));
    EXPECT_EQ(reached->Root().Scale(), (NS::Vector3{2.0f, 2.0f, 2.0f}));
}

// new_scene の 8 体は、直す前と同じ姿勢で立つ。回転とスケールは既定と違う物だけをシーンに書く
TEST(ActorFile, ShippedSceneKeepsThePosesAndWritesOnlyRotationsAndScalesThatDifferFromTheDefault)
{
    using NS::OS::FileSystem;
    const std::string path = FileSystem::Combine(
        FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "Assets"), "Scenes"), "new_scene.scene");
    nlohmann::json doc;
    ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, path));
    NS::Scene scene;
    scene.ResetFromJson(doc);

    struct Expected
    {
        NS::uint32 id;
        NS::Vector3 position;
        NS::Quaternion rotation;
        NS::Vector3 scale;
    };
    const NS::Quaternion none = NS::Quaternion::Identity;
    const NS::Vector3 one{1.0f, 1.0f, 1.0f};
    const Expected expected[] = {
        {1, {16.999433517456055f, 0.0f, 0.0f}, none, {50.0f, 1.0f, 50.0f}},
        {2, {23.228139877319336f, 1.649999976158142f, -2.528921604156494f}, none, one},
        {3, {23.228139877319336f, 4.144098281860352f, -7.358550071716309f}, none, one},
        {4, {0.0f, 0.0f, 0.0f}, {0.7382549047470093f, -0.2214764654636383f, 0.0f, 0.6371247172355652f}, one},
        {35, {18.0f, 1.0f, -7.5f}, none, one},
        {41, {18.08530616760254f, 1.25f, -2.7366538047790527f}, none, {1.5f, 1.5f, 1.5f}},
        {47, {18.0f, 1.5f, 2.5f}, none, {2.0f, 2.0f, 2.0f}},
        {53, {18.0f, 2.0f, 7.5f}, none, {3.0f, 3.0f, 3.0f}},
    };
    ASSERT_EQ(NS::SceneJsonActors(doc).size(), std::size(expected));
    for (const Expected& e : expected)
    {
        SCOPED_TRACE(e.id);
        const NS::SceneActor* actor = scene.Actors().FindByActorId(e.id);
        ASSERT_NE(actor, nullptr);
        EXPECT_EQ(actor->Root().Position(), e.position);
        EXPECT_EQ(actor->Root().Rotation(), e.rotation);
        EXPECT_EQ(actor->Root().Scale(), e.scale);

        const nlohmann::json& entry = NS::SceneJsonActors(doc)[NS::FindActorIndexById(doc, e.id)];
        const nlohmann::json* transform = NS::ClassObjFields(entry, "Transform");
        ASSERT_NE(transform, nullptr);
        EXPECT_TRUE(NS::HasField(*transform, NS::k_PositionFieldName));
        EXPECT_EQ(NS::HasField(*transform, NS::k_RotationFieldName), e.rotation != none);
        EXPECT_EQ(NS::HasField(*transform, NS::k_ScaleFieldName), e.scale != one);
    }
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
    const std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(MapObjJson(1), scene);
    ASSERT_NE(actor, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(actor.get()), 1.25f);
}

// 書く途中で失敗しても前の .actor を壊さない。壊れると、それを指すシーンのアクターが全部組めなくなる
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
    ASSERT_NO_FATAL_FAILURE(RemoveDerivedActorFiles(scope.Path()));

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
        NS::SceneJsonActors(doc).push_back(MapObjJson(1));
        NS::SceneJsonActors(doc).push_back(MapObjJson(2));
        scene.ResetFromJson(doc);
    }

    // インスペクタと同じ順で、actor の part の欄 fieldName を直し終える。値は呼ぶ前に part へ書いておく
    void EditField(LevelEditorController& editor,
                   const NS::SceneActor* actor,
                   const NS::Object* part,
                   std::string_view fieldName)
    {
        NS_ASSERT(Game, actor != nullptr, "EditField: 渡したアクターか部品が無い");
        const NS::ClassObjInfo* row = RowOf(actor, part);
        ASSERT_NE(row, nullptr);
        editor.SelectActorById(actor->Id());
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

// シーンで上書きできない欄を直し終えると .actor のファイルへ書き、同じ .actor のアクターの全部に効く。戻すと全部が前の値へ戻る
TEST(ActorFile, EditorWritesAnUnmarkedFieldToTheActorFileForEveryActor)
{
    const ScopedActorFileDirectory scope("EditorWritesActorFile");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    NS::SceneActor* first = scene.Actors().FindByActorId(1);
    ASSERT_NE(first, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(first->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, first, sphere, "半径"));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 3.0f);

    NS::Editor::ActorSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(1)), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 2.0f);

    ASSERT_TRUE(editor.Editor().Undo().Redo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(1)), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 3.0f);
}

// 欄を直した後に動作の窓が .actor の動作を書いても、欄の取り消しとやり直しは欄だけを戻し、動作は残る
TEST(ActorFile, UndoingAFieldEditKeepsActionsWrittenAfterIt)
{
    const ScopedActorFileDirectory scope("UndoKeepsActions");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    NS::SceneActor* first = scene.Actors().FindByActorId(1);
    ASSERT_NE(first, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(first->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, first, sphere, "半径"));

    // 動作の窓は取り消しの履歴を通さずに .actor の文書を書く
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    nlohmann::json document = *library.Find("PropActor");
    NS::WriteActorActions(document, {NS::ActorAction{"Jump", {}, {}}});
    ASSERT_TRUE(library.Set("PropActor", std::move(document)));

    NS::Editor::ActorSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 2.0f);
    std::vector<NS::ActorAction> actions = NS::ReadActorActions("PropActor", *library.Find("PropActor"));
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].name, "Jump");

    ASSERT_TRUE(editor.Editor().Undo().Redo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    actions = NS::ReadActorActions("PropActor", *library.Find("PropActor"));
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].name, "Jump");
}

// シーンで上書きできる欄を直すと、その 1 体だけが変わり、.actor は変わらない
TEST(ActorFile, EditorWritesAMarkedFieldToThatActorOnly)
{
    const ScopedActorFileDirectory scope("EditorWritesPlacement");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    GL::Level::PropActor* first = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(1));
    ASSERT_NE(first, nullptr);
    const nlohmann::json before = *NS::ActorFileLibrary::Get().Find("PropActor");
    const std::string path = NS::OS::FileSystem::Combine(scope.Path(), "PropActor.actor");
    const std::optional<std::string> fileBefore = NS::OS::FileSystem::ReadAllText(path);
    const float otherDiameter = DiameterOf(scene.Actors().FindByActorId(2));

    ASSERT_EQ(NS::ApplyJsonFields(first->ShadowClassObj(), DiameterEntry(otherDiameter + 3.0f)), 0u);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, first, first->ShadowClassObj(), "基本直径"));
    EXPECT_EQ(*NS::ActorFileLibrary::Get().Find("PropActor"), before);
    EXPECT_EQ(NS::OS::FileSystem::ReadAllText(path), fileBefore);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(2)), otherDiameter);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(1)), otherDiameter + 3.0f);
}

// プレイ中にシーンで上書きできない欄を直しても .actor に書き、プレイ開始の凍結は変えない。編集へ戻っても値が残り、WARN は出ない
TEST(ActorFile, PlayEditOfAnUnmarkedFieldGoesToTheActorFileAndSurvivesReturningToEdit)
{
    const ScopedActorFileDirectory scope("PlayEditActorFile");
    NS::Scene scene;
    ResetTwoProps(scene);
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    const nlohmann::json frozen = editor.PlayBaseline();
    NS::SceneActor* first = scene.Actors().FindByActorId(1);
    ASSERT_NE(first, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(first->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, first, sphere, "半径"));
    // プレイを始めた時の JSON へ写す口に直に渡しても、.actor へ書く欄は写さない
    // 上書きの一覧に載せてから渡す。載っていないと、写さずに JSON から消す道へ入り、書く所を見る前に戻る
    const NS::ClassObjInfo* row = RowOf(first, sphere);
    ASSERT_NE(row, nullptr);
    first->AddSceneOverride(row->key, "半径");
    editor.MirrorPlayEditToBaseline(first, *row, "半径");
    EXPECT_EQ(editor.PlayBaseline(), frozen);
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 3.0f);

    const TestLogCapture log;
    editor.EnterEdit();
    EXPECT_EQ(log.WarningCount(), 0);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(1)), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 3.0f);
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
                            const NS::SceneActor* actorFile,
                            const NS::Object* part,
                            std::string_view fieldName)
    {
        NS_ASSERT(Game, actorFile != nullptr, "EditActorFileField: 渡したアクターか部品が無い");
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
    EXPECT_FLOAT_EQ(RadiusOf(selected), 2.0f);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(selected->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditActorFileField(editor, selected, sphere, "半径"));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(1)), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 3.0f);

    // 戻すと .actor とアクターの全部が前の値へ戻り、選んだ .actor も前の値を見せる
    NS::Editor::ActorSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(1)), 2.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 2.0f);
    NS::SceneActor* again = NS::Cast<NS::SceneActor>(editor.SelectedAsset());
    ASSERT_NE(again, nullptr);
    EXPECT_FLOAT_EQ(RadiusOf(again), 2.0f);
}

// 素材の窓ではシーンで上書きできる欄の .actor の値も直せる。上書きの無いアクターは付いて来て、上書きのあるアクターは残る
TEST(ActorFile, MarkedFieldEditedOnTheActorFileReachesActorsWithoutAnOverride)
{
    const ScopedActorFileDirectory scope("AssetWindowMarkedField");
    NS::ActorFileLibrary::Get().Set("PropActor", ActorFileWith("PropActor", "Shadow", DiameterEntry(2.0f)));
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(MapObjJson(1));
    nlohmann::json overridden = MapObjJson(2);
    NS::ActorJsonClassObjs(overridden)["Shadow"] = DiameterEntry(5.0f);
    NS::SceneJsonActors(doc).push_back(std::move(overridden));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);
    GL::Level::PropActor* selected = NS::Cast<GL::Level::PropActor>(SelectActorFile(editor, scope.Path(), "PropActor"));
    ASSERT_NE(selected, nullptr);

    ASSERT_EQ(NS::ApplyJsonFields(selected->ShadowClassObj(), DiameterEntry(4.0f)), 0u);
    ASSERT_NO_FATAL_FAILURE(EditActorFileField(editor, selected, selected->ShadowClassObj(), "基本直径"));
    const nlohmann::json* written = NS::ActorFileLibrary::Get().Find("PropActor");
    ASSERT_NE(written, nullptr);
    const nlohmann::json* params = NS::ClassObjFields(*written, "Shadow");
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(*params, "基本直径", 0.0f), 4.0f);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(1)), 4.0f);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(2)), 5.0f);
    // 付いて来たアクターは上書きを持たないまま保存される
    const nlohmann::json saved = scene.ToJson();
    for (const nlohmann::json& object : NS::SceneJsonActors(saved))
    {
        if (NS::ActorJsonId(object) != 1u)
        {
            continue;
        }
        const nlohmann::json* savedParams = SavedFields(object, "Shadow");
        EXPECT_TRUE(savedParams == nullptr || !savedParams->contains("基本直径"));
    }

    NS::Editor::ActorSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(1)), 2.0f);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(2)), 5.0f);
}

TEST(ActorFile, ShippedActorFilesLoad)
{
    // 出荷の .actor はどれも登録済みのクラスの物で、置物はクラスが影を足す
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    ASSERT_NE(library.Find("PropActor"), nullptr);
    ASSERT_NE(library.Find("PlayerActor"), nullptr);
    const std::unique_ptr<NS::SceneActor> rockOwned = ActorFromFile("PropActor");
    ASSERT_NE(rockOwned, nullptr);
    const NS::SceneActor& rock = *rockOwned;
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
    RemoveActorFile(scope.Path(), "PropActor");
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
    refused["UnknownKey"]["timelines"] = nlohmann::json{{"Hit", 3}};
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
    RemoveActorFile(scope.Path(), "PropActor");
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

namespace
{
    // base を指し、部品 role の項目だけを書いた派生の .actor の文書
    nlohmann::json DerivedActorFileWith(std::string_view base, std::string_view role, nlohmann::json fields)
    {
        nlohmann::json actorFile = ClassOnlyActorFile("PropActor");
        NS::ActorJsonClassObjs(actorFile)[std::string{role}] = std::move(fields);
        NS::SetActorFileBase(actorFile, base);
        return actorFile;
    }

    float ValueOf(const nlohmann::json& values, std::string_view role, std::string_view field)
    {
        const nlohmann::json* fields = NS::ClassObjFields(values, role);
        if (fields == nullptr || !fields->contains(field))
        {
            return -1.0f;
        }
        return fields->at(std::string{field}).get<float>();
    }
} // namespace

// 登録した項目の行が 1 つでも欠けた .actor は、Reload も Set も ERROR を出して読まない
TEST(ActorFile, ActorFileMissingARegisteredFieldIsNotRead)
{
    const ScopedActorFileDirectory scope("MissingField");
    nlohmann::json missing = ActorFileWith("PropActor", "Params", nlohmann::json::object());
    NS::ActorJsonClassObjs(missing)["Params"].erase("摩擦");
    WriteActorFile(scope.Path(), "PropActor", missing.dump());
    ASSERT_NO_FATAL_FAILURE(RemoveDerivedActorFiles(scope.Path()));

    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    {
        const TestLogCapture log;
        library.Reload();
        EXPECT_EQ(library.Find("PropActor"), nullptr);
        EXPECT_EQ(library.Values("PropActor"), nullptr);
        EXPECT_EQ(log.ErrorCount(), 1);
    }
    {
        const TestLogCapture log;
        EXPECT_FALSE(library.Set("PropActor", missing));
        EXPECT_EQ(library.Find("PropActor"), nullptr);
        EXPECT_EQ(log.ErrorCount(), 1);
    }
}

// 派生の文書は書いた項目だけを持ち、全部の値は元の値に派生の項目を重ねた物。作ったアクターは全部の値を使う
TEST(ActorFile, DerivedActorFileOverlaysItsBase)
{
    const ScopedActorFileDirectory scope("DerivedOverlays");
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    ASSERT_TRUE(library.Set("PropActor", ActorFileWith("PropActor", "Collision", {{"半径", 2.0f}})));
    const nlohmann::json derived = DerivedActorFileWith("PropActor", "Params", MassEntry(4.0f));
    ASSERT_TRUE(library.Set("HeavyRock", derived));

    ASSERT_NE(library.Find("HeavyRock"), nullptr);
    EXPECT_EQ(*library.Find("HeavyRock"), derived);
    const nlohmann::json* values = library.Values("HeavyRock");
    ASSERT_NE(values, nullptr);
    EXPECT_FLOAT_EQ(ValueOf(*values, "Params", "質量"), 4.0f);
    EXPECT_FLOAT_EQ(ValueOf(*values, "Collision", "半径"), 2.0f);
    EXPECT_FLOAT_EQ(ValueOf(*values, "Params", "摩擦"), ValueOf(*library.Values("PropActor"), "Params", "摩擦"));

    const std::unique_ptr<NS::SceneActor> rock =
        NS::CreateActorOfClass("PropActor", NS::ActorInitInfo{.actorFile = "HeavyRock"});
    ASSERT_NE(rock, nullptr);
    EXPECT_FLOAT_EQ(MassOf(rock.get()), 4.0f);
    EXPECT_FLOAT_EQ(RadiusOf(rock.get()), 2.0f);
}

// 元を Set すると、その元の派生の全部の値も作り直す。派生に書いた項目は派生の値のまま
TEST(ActorFile, SettingTheBaseChangesTheDerivedValues)
{
    const ScopedActorFileDirectory scope("BaseChangesDerived");
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    ASSERT_TRUE(library.Set("PropActor", ActorFileWith("PropActor", "Collision", {{"半径", 2.0f}})));
    ASSERT_TRUE(library.Set("HeavyRock", DerivedActorFileWith("PropActor", "Params", MassEntry(4.0f))));

    ASSERT_TRUE(library.Set("PropActor", ActorFileWith("PropActor", "Collision", {{"半径", 3.0f}})));
    const nlohmann::json* values = library.Values("HeavyRock");
    ASSERT_NE(values, nullptr);
    EXPECT_FLOAT_EQ(ValueOf(*values, "Collision", "半径"), 3.0f);
    EXPECT_FLOAT_EQ(ValueOf(*values, "Params", "質量"), 4.0f);
}

// 派生は、元に書く項目を書いた物・無い元を指す物・派生を指す物・元とクラスの違う物を ERROR で読まない
TEST(ActorFile, DerivedActorFileThatCannotOverlayItsBaseIsNotRead)
{
    const ScopedActorFileDirectory scope("DerivedRefused");
    WriteActorFile(scope.Path(), "PropActor", ActorFileWith("PropActor", "Params", nlohmann::json::object()).dump());
    WriteActorFile(scope.Path(), "HeavyRock", DerivedActorFileWith("PropActor", "Params", MassEntry(4.0f)).dump());
    WriteActorFile(scope.Path(), "WideRock", DerivedActorFileWith("PropActor", "Collision", SphereEntry(2.0f)).dump());
    WriteActorFile(scope.Path(), "LostRock", DerivedActorFileWith("NoSuchActor", "Params", MassEntry(4.0f)).dump());
    WriteActorFile(scope.Path(), "HeavierRock", DerivedActorFileWith("HeavyRock", "Params", MassEntry(8.0f)).dump());
    nlohmann::json otherClass = DerivedActorFileWith("PropActor", "Params", MassEntry(4.0f));
    NS::SetActorFileClass(otherClass, "GoalActor");
    WriteActorFile(scope.Path(), "GoalRock", otherClass.dump());

    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    const TestLogCapture log;
    library.Reload();
    EXPECT_NE(library.Values("HeavyRock"), nullptr);
    for (const char* name : {"WideRock", "LostRock", "HeavierRock", "GoalRock"})
    {
        EXPECT_EQ(library.Find(name), nullptr) << name;
        EXPECT_EQ(library.Values(name), nullptr) << name;
    }
    // 元に書く項目を書いた派生は、項目の ERROR と読まない ERROR の 2 つを出す
    EXPECT_EQ(log.ErrorCount(), 5);
}

// 演出と動作は元の .actor だけが持つ。派生に書いてあると効かない理由が分からないので、ERROR で読まない
TEST(ActorFile, DerivedActorFileWithHitReactionsOrActionsIsNotRead)
{
    const ScopedActorFileDirectory scope("DerivedLists");
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    for (const char* key : {"hitReactions", "actions"})
    {
        SCOPED_TRACE(key);
        nlohmann::json derived = DerivedActorFileWith("PropActor", "Params", MassEntry(4.0f));
        derived[key] = nlohmann::json::array();
        const TestLogCapture log;
        EXPECT_FALSE(library.Set("HeavyRock", derived));
        EXPECT_EQ(library.Find("HeavyRock"), nullptr);
        EXPECT_EQ(library.Values("HeavyRock"), nullptr);
        EXPECT_EQ(log.ErrorCount(), 1);
    }
}

// .actor の無い名前ではアクターを作らない。コードの値だけで組むと、どの値で動くかがファイルから分からない
// 置き場が無いディレクトリへ替えると、前の置き場の文書も全部の値も残さず、アクターを作らない
TEST(ActorFile, DirectoryThatDoesNotExistLeavesNoValuesOfThePreviousOne)
{
    const ScopedActorFileDirectory scope("MissingDirectory");
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    ASSERT_NE(library.Values("PropActor"), nullptr);

    library.SetDirectory(NS::OS::FileSystem::Combine(scope.Path(), "NoSuchDirectory"));
    EXPECT_EQ(library.Find("PropActor"), nullptr);
    EXPECT_EQ(library.Values("PropActor"), nullptr);
    const TestLogCapture log;
    EXPECT_EQ(NS::CreateActorOfClass("PropActor"), nullptr);
}

TEST(ActorFile, NoActorIsCreatedWithoutItsActorFile)
{
    const ScopedActorFileDirectory scope("NoActorFile");
    NS::ActorFileLibrary::Get().Erase("PropActor");
    const TestLogCapture log;
    EXPECT_EQ(NS::CreateActorOfClass("PropActor"), nullptr);
    EXPECT_EQ(NS::CreateActorOfClass("PropActor", NS::ActorInitInfo{.actorFile = "NoSuchRock"}), nullptr);
    EXPECT_EQ(log.ErrorCount(), 2);
}

// 元の .actor を書くと、元を指すアクターと派生を指すアクターの全部に全部の値が写る。
// 派生が書いた項目 (影の直径 4) と、シーンで上書きした項目 (影の直径 9) は残る
TEST(ActorFile, WritingTheBaseUpdatesDerivedActorsAndKeepsSceneOverrides)
{
    const ScopedActorFileDirectory scope("WriteBaseToDerived");
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    ASSERT_TRUE(library.Set("HeavyRock", DerivedActorFileWith("PropActor", "Shadow", DiameterEntry(4.0f))));

    nlohmann::json derived = MapObjJson(2);
    NS::SetActorJsonFile(derived, "HeavyRock");
    nlohmann::json overridden = MapObjJson(3);
    NS::ActorJsonClassObjs(overridden)["Shadow"] = DiameterEntry(9.0f);
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(MapObjJson(1));
    NS::SceneJsonActors(doc).push_back(std::move(derived));
    NS::SceneJsonActors(doc).push_back(std::move(overridden));
    NS::Scene scene;
    scene.ResetFromJson(doc);
    LevelEditorController editor(&scene);
    GL::Level::PropActor* selected = NS::Cast<GL::Level::PropActor>(SelectActorFile(editor, scope.Path(), "PropActor"));
    ASSERT_NE(selected, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(selected->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditActorFileField(editor, selected, sphere, "半径"));
    ASSERT_EQ(NS::ApplyJsonFields(selected->ShadowClassObj(), DiameterEntry(5.0f)), 0u);
    ASSERT_NO_FATAL_FAILURE(EditActorFileField(editor, selected, selected->ShadowClassObj(), "基本直径"));

    for (NS::uint32 id : {1u, 2u, 3u})
    {
        EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(id)), 3.0f) << id;
    }
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(1)), 5.0f);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(2)), 4.0f);
    EXPECT_FLOAT_EQ(DiameterOf(scene.Actors().FindByActorId(3)), 9.0f);
}

// 書く先が元の .actor なら、派生を指すアクターでも元の名前へ書く。派生の .actor なら指す名前、シーンなら空
TEST(ActorFile, ActorFileToWriteFollowsTheDestination)
{
    const ScopedActorFileDirectory scope("ActorFileToWrite");
    const ScopedActorFile originFile{"PropActor", CodeValuesActorFile("PropActor")};
    const ScopedActorFile derivedFile{"HeavyRock", DerivedActorFileWith("PropActor", "Params", MassEntry(4.0f))};
    const std::unique_ptr<NS::SceneActor> origin = ActorFromFile("PropActor");
    const std::unique_ptr<NS::SceneActor> derived = ActorFromFile("HeavyRock");
    ASSERT_NE(origin, nullptr);
    ASSERT_NE(derived, nullptr);

    EXPECT_EQ(NS::ActorFileToWrite(origin.get(), NS::FieldDestination::ActorFile), "PropActor");
    EXPECT_EQ(NS::ActorFileToWrite(derived.get(), NS::FieldDestination::ActorFile), "PropActor");
    EXPECT_EQ(NS::ActorFileToWrite(derived.get(), NS::FieldDestination::DerivedActorFile), "HeavyRock");
    EXPECT_EQ(NS::ActorFileToWrite(derived.get(), NS::FieldDestination::Scene), "");
}

namespace
{
    // PropActor.actor (半径 2) と、それを元にする HeavyRock.actor (質量 4) を置き場へ書き、
    // 1 番に PropActor・2 番に HeavyRock を指す置物を置いたシーン
    void ResetPropAndHeavyRock(NS::Scene& scene)
    {
        NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
        ASSERT_TRUE(library.Set("PropActor", ActorFileWith("PropActor", "Collision", SphereEntry(2.0f))));
        ASSERT_TRUE(library.Save("PropActor"));
        ASSERT_TRUE(library.Set("HeavyRock", DerivedActorFileWith("PropActor", "Params", MassEntry(4.0f))));
        ASSERT_TRUE(library.Save("HeavyRock"));
        nlohmann::json heavy = MapObjJson(2);
        NS::SetActorJsonFile(heavy, "HeavyRock");
        nlohmann::json doc = NS::MakeSceneJson();
        NS::SceneJsonActors(doc).push_back(MapObjJson(1));
        NS::SceneJsonActors(doc).push_back(std::move(heavy));
        scene.ResetFromJson(doc);
    }

    // directory/name.actor を読んだ文書。読めなければ null
    nlohmann::json SavedActorFile(const std::string& directory, std::string_view name)
    {
        const std::optional<std::string> text =
            NS::OS::FileSystem::ReadAllText(NS::OS::FileSystem::Combine(directory, std::string{name} + ".actor"));
        if (!text.has_value())
        {
            return nullptr;
        }
        return nlohmann::json::parse(*text);
    }
} // namespace

// 派生を指すアクターで共有の項目を直すと、元の .actor へ書き、派生の .actor は変えない
TEST(ActorFile, SharedFieldEditedOnADerivedActorIsWrittenToTheBase)
{
    const ScopedActorFileDirectory scope("SharedFieldOnDerived");
    NS::Scene scene;
    ASSERT_NO_FATAL_FAILURE(ResetPropAndHeavyRock(scene));
    LevelEditorController editor(&scene);
    const nlohmann::json derivedBefore = *NS::ActorFileLibrary::Get().Find("HeavyRock");
    NS::SceneActor* heavy = scene.Actors().FindByActorId(2);
    ASSERT_NE(heavy, nullptr);
    NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(heavy->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);

    sphere->SetRadius(3.0f);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, heavy, sphere, "半径"));
    EXPECT_FLOAT_EQ(SavedRadius(scope.Path()), 3.0f);
    EXPECT_EQ(*NS::ActorFileLibrary::Get().Find("HeavyRock"), derivedBefore);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(1)), 3.0f);
    EXPECT_FLOAT_EQ(RadiusOf(scene.Actors().FindByActorId(2)), 3.0f);
}

// 素材の窓で .actor から派生を作ると、元を指して何も書いていない .actor をファイルへ書く。
// 派生から作っても、その派生の元を指して何も書いていない .actor になり、派生が書いた項目は写さない。
// 派生を指すアクターから作ると、アクターの値は元の値に戻り、取り消すと前の派生と値へ戻る。
// ある名前と、ファイルの名前に使えない名前では作らない
TEST(ActorFile, CreatingADerivedActorFileWritesOnlyTheBase)
{
    const ScopedActorFileDirectory scope("CreateDerived");
    NS::Scene scene;
    ASSERT_NO_FATAL_FAILURE(ResetPropAndHeavyRock(scene));
    LevelEditorController editor(&scene);
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();

    ASSERT_TRUE(editor.CreateDerivedActorFile("PropActor", "WideRock"));
    const nlohmann::json wide = SavedActorFile(scope.Path(), "WideRock");
    EXPECT_EQ(NS::ActorFileBase(wide), "PropActor");
    EXPECT_EQ(NS::ActorFileClass(wide), "PropActor");
    EXPECT_TRUE(NS::ActorJsonClassObjs(wide).empty());
    ASSERT_NE(library.Values("WideRock"), nullptr);
    EXPECT_EQ(*library.Values("WideRock"), *library.Values("PropActor"));

    ASSERT_TRUE(editor.CreateDerivedActorFile("HeavyRock", "HeavierRock"));
    const nlohmann::json heavier = SavedActorFile(scope.Path(), "HeavierRock");
    EXPECT_EQ(NS::ActorFileBase(heavier), "PropActor");
    EXPECT_TRUE(NS::ActorJsonClassObjs(heavier).empty());
    ASSERT_NE(library.Values("HeavierRock"), nullptr);
    EXPECT_EQ(*library.Values("HeavierRock"), *library.Values("PropActor"));

    const float baseMass = ValueOf(*library.Values("PropActor"), "Params", "質量");
    ASSERT_NE(baseMass, 4.0f);
    ASSERT_TRUE(editor.DeriveActorFileFromActor(2, "TunedHeavyRock"));
    const nlohmann::json tuned = SavedActorFile(scope.Path(), "TunedHeavyRock");
    EXPECT_EQ(NS::ActorFileBase(tuned), "PropActor");
    EXPECT_TRUE(NS::ActorJsonClassObjs(tuned).empty());
    EXPECT_EQ(scene.Actors().FindByActorId(2)->ActorFileName(), "TunedHeavyRock");
    EXPECT_FLOAT_EQ(MassOf(scene.Actors().FindByActorId(2)), baseMass);
    NS::Editor::ActorSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    EXPECT_EQ(scene.Actors().FindByActorId(2)->ActorFileName(), "HeavyRock");
    EXPECT_FLOAT_EQ(MassOf(scene.Actors().FindByActorId(2)), 4.0f);

    const TestLogCapture log;
    EXPECT_FALSE(editor.CreateDerivedActorFile("PropActor", "HeavyRock"));
    EXPECT_FALSE(editor.CreateDerivedActorFile("PropActor", ""));
    EXPECT_FALSE(editor.CreateDerivedActorFile("PropActor", "Bad/Rock"));
    EXPECT_FALSE(editor.CreateDerivedActorFile("NoSuchActor", "LostRock"));
    EXPECT_FLOAT_EQ(ValueOf(*library.Find("HeavyRock"), "Params", "質量"), 4.0f);
    EXPECT_EQ(library.Find("LostRock"), nullptr);
    EXPECT_EQ(log.WarningCount(), 4);
}

// シーンで詰めたアクターから派生を作ると、上書きを派生の .actor へ移し、アクターをその派生に切り替える。
// 値と位置は変わらず、保存は上書きを書かない。取り消すと元の .actor と上書きへ戻る
TEST(ActorFile, DerivingFromASceneActorMovesItsOverridesToTheDerivedActorFile)
{
    const ScopedActorFileDirectory scope("DeriveFromActor");
    NS::Scene scene;
    ASSERT_NO_FATAL_FAILURE(ResetPropAndHeavyRock(scene));
    LevelEditorController editor(&scene);
    GL::Level::PropActor* first = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(1));
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(first->ShadowClassObj(), DiameterEntry(9.0f)), 0u);
    ASSERT_NO_FATAL_FAILURE(EditField(editor, first, first->ShadowClassObj(), "基本直径"));
    const nlohmann::json before = scene.ToJson();
    const nlohmann::json& beforeObject = NS::SceneJsonActors(before)[NS::FindActorIndexById(before, 1)];

    ASSERT_TRUE(editor.DeriveActorFileFromActor(1, "TunedRock"));
    const nlohmann::json tuned = SavedActorFile(scope.Path(), "TunedRock");
    EXPECT_EQ(NS::ActorFileBase(tuned), "PropActor");
    EXPECT_FLOAT_EQ(ValueOf(tuned, "Shadow", "基本直径"), 9.0f);
    const NS::SceneActor* moved = scene.Actors().FindByActorId(1);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved, first);
    EXPECT_EQ(moved->ActorFileName(), "TunedRock");
    EXPECT_FALSE(moved->IsSceneOverride("Shadow", "基本直径"));
    EXPECT_FLOAT_EQ(DiameterOf(moved), 9.0f);
    const nlohmann::json after = scene.ToJson();
    const nlohmann::json& afterObject = NS::SceneJsonActors(after)[NS::FindActorIndexById(after, 1)];
    EXPECT_FALSE(NS::HasField(*NS::ClassObjFields(afterObject, "Shadow"), "基本直径"));
    EXPECT_EQ(*SavedFields(afterObject, "Transform"), *SavedFields(beforeObject, "Transform"));

    NS::Editor::ActorSnapshotApplier applier{&scene};
    ASSERT_TRUE(editor.Editor().Undo().Undo(applier));
    const NS::SceneActor* restored = scene.Actors().FindByActorId(1);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored, first);
    EXPECT_EQ(restored->ActorFileName(), "PropActor");
    EXPECT_TRUE(restored->IsSceneOverride("Shadow", "基本直径"));
    EXPECT_FLOAT_EQ(DiameterOf(restored), 9.0f);
}

// 元の .actor の束を書き替えると、元を指すアクターにも派生を指すアクターにも新しい束が効く
TEST(ActorFile, WritingTheBaseHitReactionsReachesActorsOfItsDerived)
{
    const ScopedActorFileDirectory scope("BaseHitReactions");
    NS::Scene scene;
    ASSERT_NO_FATAL_FAILURE(ResetPropAndHeavyRock(scene));
    LevelEditorController editor(&scene);
    NS::HitReactionSet set;
    set.name = "Hit";
    set.stopFrames = 12;
    editor.WriteActorFileHitReactions("PropActor", {set});
    set.stopFrames = 20;
    editor.WriteActorFileHitReactions("PropActor", {set});

    for (NS::uint32 id : {1u, 2u})
    {
        const NS::HitReaction* reaction = scene.Actors().FindByActorId(id)->HitReactionClassObj();
        ASSERT_NE(reaction, nullptr) << id;
        const NS::HitReactionSet* found = reaction->FindSet("Hit");
        ASSERT_NE(found, nullptr) << id;
        EXPECT_EQ(found->stopFrames, 20) << id;
    }
}
