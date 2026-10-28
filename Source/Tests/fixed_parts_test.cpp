#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/SensorKinds.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/Actors/MapPartsActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    template <class T> constexpr bool k_HasPartName = requires(const T& part) { part.Name(); };
    template <class T>
    constexpr bool k_HasReflectedParts = requires(const T& info) {
        info.classObjs;
        info.classObjCount;
    };

    class MemberCreationProbeActor final : public NS::SceneActor
    {
    public:
        template <class TSelf = MemberCreationProbeActor> static constexpr bool CanCreateWithoutName()
        {
            return requires(TSelf& self) { self.CreateClassObj(self.part, &self); };
        }

    private:
        std::unique_ptr<NS::Model> part;
    };

    template <class T>
    constexpr bool k_CanCreatePartByName = requires(T& actor) { actor.CreateClassObj(std::string_view{"Model"}); };

    class DirectMemberActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(DirectMemberActor, NS::SceneActor)
        NS_CLASSOBJ(shape, "Shape")
        NS_REFLECT_END()

        std::unique_ptr<NS::Collision> shape;

    protected:
        void OnInit() override { CreateClassObj<NS::Collision, NS::SphereCollision>(shape); }

    public:
        template <class TSelf = DirectMemberActor> static void CheckTypes()
        {
            static_assert(requires(TSelf& self) {
                self.template CreateClassObj<NS::Collision, NS::SphereCollision>(self.shape);
            });
            static_assert(
                !requires(TSelf& self) { self.template CreateClassObj<NS::Collision, NS::Model>(self.shape); });
        }
    };

    class ModelPartActor final : public NS::SceneActor
    {
    protected:
        void OnInit() override { CreateClassObj(m_model); }
    };

    class SensorPartsActor final : public NS::SceneActor
    {
    private:
        NS_SENSORS_BEGIN()
        NS_SENSOR(m_bodySensor, "Body", GL::Level::SensorKind::Unset, NS::HitSensorShape::Sphere)
        NS_SENSOR(m_attackSensor, "Attack", GL::Level::SensorKind::Unset, NS::HitSensorShape::Sphere)
        NS_SENSORS_END()
        NS::HitSensor* m_bodySensor = nullptr;
        NS::HitSensor* m_attackSensor = nullptr;
    };

    // 表を回して鍵 key の部品を返す。出なければ nullptr
    NS::Object* PartAt(const NS::SceneActor* actor, std::string_view key)
    {
        NS_ASSERT(Game, actor != nullptr, "PartAt: 渡したアクターか部品が無い");
        NS::Object* found = nullptr;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& field, NS::Object* part) {
            if (std::string_view{field.key} == key)
            {
                found = part;
            }
        });
        return found;
    }

    // 表を回して part の鍵を返す。表に無ければ空
    std::string_view KeyOf(const NS::SceneActor* actor, const NS::Object* part)
    {
        NS_ASSERT(Game, actor != nullptr && part != nullptr, "KeyOf: 渡したアクターか部品が無い");
        std::string_view key;
        NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& field, const NS::Object* candidate) {
            if (candidate == part)
            {
                key = field.key;
            }
        });
        return key;
    }

    class MemberOwnedPart final : public NS::Object
    {
    public:
        MemberOwnedPart(std::vector<std::string>& destroyed, std::string name)
            : m_destroyed(destroyed), m_name(std::move(name))
        {}

        ~MemberOwnedPart() override { m_destroyed.push_back(m_name); }

    private:
        std::vector<std::string>& m_destroyed;
        std::string m_name;
    };

    class MemberOwnedActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(MemberOwnedActor, NS::SceneActor)
        NS_CLASSOBJ(first, "First")
        NS_CLASSOBJ(second, "Second")
        NS_REFLECT_END()

        explicit MemberOwnedActor(std::vector<std::string>& destroyed) : m_destroyed(destroyed) {}
        std::unique_ptr<MemberOwnedPart> first;
        std::unique_ptr<MemberOwnedPart> second;

        MemberOwnedPart* ReplaceFirst() { return CreateClassObj(first, m_destroyed, std::string{"replacement"}); }

    protected:
        void OnInit() override
        {
            CreateClassObj(first, m_destroyed, std::string{"first"});
            CreateClassObj(second, m_destroyed, std::string{"second"});
        }

    private:
        std::vector<std::string>& m_destroyed;
    };

    // 部品は自分からは動かない。基底に更新の関数があると、持ち主が全部の部品へ回す道が戻る
    template <class TClassObj = NS::Object> void CheckClassObjHasNoUpdate()
    {
        static_assert(!requires(TClassObj& part) { part.OnUpdate(); });
    }

    template <class TClassObj = NS::Object> void CheckClassObjHasNoOnOff()
    {
        static_assert(!requires(TClassObj& part) { part.SetActive(false); });
        static_assert(!requires(const TClassObj& part) { part.IsActive(); });
        static_assert(!requires(const TClassObj& part) { part.IsActiveSelf(); });
    }

    class ReflectedProbePart final : public NS::Object
    {
    public:
        float value = 0.0f;
        NS_REFLECT_BEGIN(ReflectedProbePart, NS::Object)
        // アクターの保存と読み直しで、部品の鍵ごとに値が戻るかを見るので、シーンで上書きできる欄にする
        NS_GLOBAL_FIELD(value, "値")
        NS_REFLECT_END()
    };

    class ReflectedParentActor : public NS::SceneActor
    {
    public:
        std::unique_ptr<ReflectedProbePart> parentPart;
        NS_REFLECT_BEGIN(ReflectedParentActor, NS::SceneActor)
        NS_CLASSOBJ(parentPart, "ParentPart")
        NS_REFLECT_END()

    protected:
        void OnInit() override { CreateClassObj(parentPart); }
    };

    class ReflectedHierarchyActor final : public ReflectedParentActor
    {
    public:
        std::unique_ptr<ReflectedProbePart> childPart;
        std::unique_ptr<ReflectedProbePart> absentPart;
        NS_REFLECT_BEGIN(ReflectedHierarchyActor, ReflectedParentActor)
        NS_CLASSOBJ(childPart, "ChildPart")
        NS_CLASSOBJ(absentPart, "AbsentPart")
        NS_REFLECT_END()

    protected:
        void OnInit() override
        {
            ReflectedParentActor::OnInit();
            CreateClassObj(childPart);
        }
    };
    NS_CLASS(ReflectedHierarchyActor)

    class UnregisteredPartActor final : public NS::SceneActor
    {
    public:
        std::unique_ptr<ReflectedProbePart> part;

    protected:
        void OnInit() override { CreateClassObj(part); }
    };

    class OffsetPrefix
    {
    public:
        virtual ~OffsetPrefix() = default;
        int prefix = 0;
    };

    class OffsetPartActor final : public OffsetPrefix, public NS::SceneActor
    {
    public:
        std::unique_ptr<ReflectedProbePart> part;
        NS_REFLECT_BEGIN(OffsetPartActor, NS::SceneActor)
        NS_CLASSOBJ(part, "OffsetPart")
        NS_REFLECT_END()

    protected:
        void OnInit() override { CreateClassObj(part); }
    };

} // namespace

TEST(ReflectedParts, PartsDoNotStoreAnInstanceName)
{
    EXPECT_FALSE(k_HasPartName<NS::Object>);
}

TEST(ReflectedParts, CreationOnlyNeedsTheOwningMember)
{
    EXPECT_TRUE(MemberCreationProbeActor::CanCreateWithoutName());
}

TEST(ReflectedParts, ReflectionDescribesOwnedParts)
{
    EXPECT_TRUE(k_HasReflectedParts<NS::ReflectionInfo>);
}

TEST(ReflectedParts, InheritedMembersDriveEnumerationWithoutAnOverride)
{
    const std::unique_ptr<ReflectedHierarchyActor> actor = NS::CreateActor<ReflectedHierarchyActor>();
    ASSERT_NE(actor->parentPart, nullptr);
    ASSERT_NE(actor->childPart, nullptr);
    EXPECT_EQ(actor->absentPart, nullptr);
    std::vector<std::string> keys;
    NS::ForEachReflectedClassObj(actor.get(),
                                 [&](const NS::ClassObjInfo& field, NS::Object*) { keys.emplace_back(field.key); });
    EXPECT_EQ(keys, (std::vector<std::string>{"Transform", "HitSensors", "ParentPart", "ChildPart"}));
    EXPECT_EQ(NS::Cast<ReflectedParentActor>(actor.get()), actor.get());
}

TEST(ReflectedParts, SameTypedMembersSaveAndReloadThroughTheirRegisteredKeys)
{
    // 同じ型の 2 つの部品の値は、それぞれの鍵で .actor から入り、保存と読み直しでも鍵が入れ替わらない
    nlohmann::json actorFile = CodeValuesActorFile("ReflectedHierarchyActor");
    NS::ActorJsonClassObjs(actorFile)["ParentPart"]["値"] = 2.0f;
    NS::ActorJsonClassObjs(actorFile)["ChildPart"]["値"] = 7.0f;
    const ScopedActorFile hierarchyFile{"ReflectedHierarchyActor", actorFile};
    const std::unique_ptr<ReflectedHierarchyActor> actor = NS::CreateActor<ReflectedHierarchyActor>();
    EXPECT_FLOAT_EQ(actor->parentPart->value, 2.0f);
    EXPECT_FLOAT_EQ(actor->childPart->value, 7.0f);
    const nlohmann::json saved = NS::ActorToJson(actor.get());
    EXPECT_TRUE(NS::ActorJsonClassObjs(saved).contains("ParentPart"));
    EXPECT_TRUE(NS::ActorJsonClassObjs(saved).contains("ChildPart"));
    EXPECT_FALSE(NS::ActorJsonClassObjs(saved).contains("AbsentPart"));
    NS::Scene scene;
    const std::unique_ptr<NS::SceneActor> loaded = NS::ActorFromJson(saved, scene);
    const ReflectedHierarchyActor* restored = NS::Cast<ReflectedHierarchyActor>(loaded.get());
    ASSERT_NE(restored, nullptr);
    EXPECT_FLOAT_EQ(restored->parentPart->value, 2.0f);
    EXPECT_FLOAT_EQ(restored->childPart->value, 7.0f);
    EXPECT_EQ(KeyOf(restored, restored->parentPart.get()), "ParentPart");
    EXPECT_EQ(KeyOf(restored, restored->childPart.get()), "ChildPart");
    const ReflectedProbePart foreign;
    EXPECT_TRUE(KeyOf(restored, &foreign).empty());
}

TEST(ReflectedParts, UnregisteredMembersCannotCreateInvisibleParts)
{
    const TestLogCapture log;
    const std::unique_ptr<UnregisteredPartActor> actor = NS::CreateActor<UnregisteredPartActor>();
    EXPECT_EQ(log.ErrorCount(), 1);
    EXPECT_EQ(actor->part, nullptr);
}

TEST(ReflectedParts, AccessorsAdjustTheActorBaseAddress)
{
    const std::unique_ptr<OffsetPartActor> actor = NS::CreateActor<OffsetPartActor>();
    ASSERT_NE(actor->part, nullptr);
    EXPECT_EQ(PartAt(actor.get(), "OffsetPart"), actor->part.get());
    EXPECT_EQ(NS::Cast<OffsetPartActor>(static_cast<NS::Object*>(actor.get())), actor.get());
}

TEST(FixedParts, PartCreationRequiresAnOwningMember)
{
    EXPECT_FALSE(k_CanCreatePartByName<NS::SceneActor>);
}

TEST(FixedParts, MemberTypeCanOwnADerivedPartWithoutASlot)
{
    DirectMemberActor::CheckTypes();
    const std::unique_ptr<DirectMemberActor> actor = NS::CreateActor<DirectMemberActor>();
    ASSERT_NE(actor->shape, nullptr);
    EXPECT_NE(NS::Cast<NS::SphereCollision>(actor->shape.get()), nullptr);
    EXPECT_EQ(PartAt(actor.get(), "Shape"), actor->shape.get());
}

TEST(FixedParts, MapObjCreatesShadowBeforeActorFileIsApplied)
{
    const std::unique_ptr<GL::Level::PropActor> actor = NS::CreateActor<GL::Level::PropActor>();
    ASSERT_NE(actor->ShadowClassObj(), nullptr);
    EXPECT_EQ(PartAt(actor.get(), "Shadow"), actor->ShadowClassObj());
}

TEST(FixedParts, MemberOwnershipSupportsLookupAndDestruction)
{
    std::vector<std::string> destroyed;
    {
        const std::unique_ptr<MemberOwnedActor> owned = NS::CreateActor<MemberOwnedActor>(destroyed);
        MemberOwnedActor& actor = *owned;
        ASSERT_NE(actor.first, nullptr);
        ASSERT_NE(actor.second, nullptr);
        EXPECT_EQ(PartAt(&actor, "First"), actor.first.get());
        EXPECT_EQ(PartAt(&actor, "Second"), actor.second.get());
        const TestLogCapture log;
        EXPECT_EQ(actor.ReplaceFirst(), nullptr);
        EXPECT_EQ(log.ErrorCount(), 1);
        EXPECT_EQ(PartAt(&actor, "First"), actor.first.get());
        EXPECT_EQ(PartAt(&actor, "Replacement"), nullptr);
    }
    EXPECT_EQ(destroyed, (std::vector<std::string>{"second", "first"}));
}

TEST(FixedParts, TransformUsesTheSamePartLookupAndFieldEditing)
{
    NS::SceneActor actor;
    NS::Transform* transform = NS::Cast<NS::Transform>(PartAt(&actor, "Transform"));
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(&actor.Root(), transform);
    NS::ApplyJsonFields(transform, nlohmann::json{{"位置", {2.0f, 3.0f, 4.0f}}});
    EXPECT_EQ(actor.Root().Position(), (NS::Vector3{2.0f, 3.0f, 4.0f}));
    // 姿勢はグローバルの項目なので、シーンの上書きの一覧に載せた物を保存で書く
    actor.AddSceneOverride("Transform", "位置");
    EXPECT_EQ(NS::ActorToJson(&actor)["classObjs"]["Transform"]["位置"], (nlohmann::json::array({2.0f, 3.0f, 4.0f})));
}

// 外から来る大きさだけを下限で止める。コードからの値は跡を 0 まで縮めるので止めない
TEST(FixedParts, TransformClampsOnlyTheScaleFromTheField)
{
    NS::SceneActor actor;
    NS::ApplyJsonFields(&actor.Root(), nlohmann::json{{"スケール", {0.001f, -1.0f, 2.0f}}});
    EXPECT_EQ(actor.Root().Scale(), (NS::Vector3{0.01f, 0.01f, 2.0f}));

    actor.Root().SetScale(NS::Vector3{0.0f, 1.0f, 0.0f});
    EXPECT_EQ(actor.Root().Scale(), (NS::Vector3{0.0f, 1.0f, 0.0f}));
}

TEST(FixedParts, ClassObjBaseHasNoUpdate)
{
    CheckClassObjHasNoUpdate();
}

TEST(FixedParts, ClassObjBaseHasNoOnOff)
{
    CheckClassObjHasNoOnOff();
}

TEST(FixedParts, CommonMembersAreCreatedByTheActorClass)
{
    const NS::SceneActor empty;
    EXPECT_EQ(empty.ModelClassObj(), nullptr);
    const std::unique_ptr<ModelPartActor> owned = NS::CreateActor<ModelPartActor>();
    NS::SceneActor& actor = *owned;
    NS::Model* model = actor.ModelClassObj();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(PartAt(&actor, "Model"), model);
    std::vector<std::string> names;
    NS::ForEachReflectedClassObj(&actor,
                                 [&](const NS::ClassObjInfo& field, NS::Object*) { names.push_back(field.key); });
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model", "HitSensors"}));
}

// センサーは部品の一覧に 1 つずつ出ず、センサーの部品 1 つが名前つきで持つ
TEST(FixedParts, SensorsLiveInOneSensorsPartByName)
{
    const std::unique_ptr<SensorPartsActor> owned = NS::CreateActor<SensorPartsActor>();
    NS::SceneActor& actor = *owned;
    NS::Object* body = actor.HitSensorsObjClassObj()->Find("Body");
    NS::Object* attack = actor.HitSensorsObjClassObj()->Find("Attack");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(attack, nullptr);
    EXPECT_NE(body, attack);
    EXPECT_EQ(PartAt(&actor, "HitSensors"), actor.HitSensorsObjClassObj());
    EXPECT_EQ(PartAt(&actor, "BodySensor"), nullptr);
    EXPECT_EQ(PartAt(&actor, "Missing"), nullptr);
    EXPECT_NE(NS::Cast<NS::ShapeHitSensor>(body), nullptr);
    EXPECT_NE(NS::Cast<NS::ShapeHitSensor>(attack), nullptr);
}

TEST(FixedParts, ConcreteActorsExposeTheirOwnedRoles)
{
    const std::unique_ptr<PlayerActor> ownedPlayer = NS::CreateActor<PlayerActor>();
    const std::unique_ptr<GL::Level::PropActor> ownedRock = NS::CreateActor<GL::Level::PropActor>();
    const std::unique_ptr<GL::Level::FollowCameraActor> ownedCamera = NS::CreateActor<GL::Level::FollowCameraActor>();
    const std::unique_ptr<NS::LightActor> ownedLight = NS::CreateActor<NS::LightActor>();
    const std::unique_ptr<NS::MapPartsActor> ownedTerrain = NS::CreateActor<NS::MapPartsActor>();
    PlayerActor& player = *ownedPlayer;
    GL::Level::PropActor& rock = *ownedRock;
    GL::Level::FollowCameraActor& camera = *ownedCamera;
    NS::LightActor& light = *ownedLight;
    NS::MapPartsActor& terrain = *ownedTerrain;
    EXPECT_EQ(PartAt(&player, "Input"), &player.Input());
    EXPECT_EQ(PartAt(&player, "Movement"), &player.Body());
    EXPECT_NE(PartAt(&player, "Params"), nullptr);
    EXPECT_NE(player.ModelClassObj(), nullptr);
    EXPECT_NE(player.HitSensorsObjClassObj()->Find("Body"), nullptr);
    EXPECT_NE(rock.ModelClassObj(), nullptr);
    EXPECT_NE(rock.CollisionClassObj(), nullptr);
    EXPECT_NE(PartAt(&rock, "Params"), nullptr);
    EXPECT_NE(PartAt(&rock, "LaunchEffects"), nullptr);
    EXPECT_EQ(PartAt(&camera, "Vcam"), &camera.Vcam());
    EXPECT_NE(PartAt(&light, "DirectionalLight"), nullptr);
    EXPECT_NE(terrain.ModelClassObj(), nullptr);
    EXPECT_NE(terrain.CollisionClassObj(), nullptr);
}

TEST(FixedParts, SavedActorsUseRoleKeysAndDirectFields)
{
    const nlohmann::json source = {{"actor", "PropActor"}, {"id", 7u}, {"classObjs", {{"Shadow", {{"基本直径", 2.0f}}}}}};
    NS::Scene scene;
    std::unique_ptr<NS::SceneActor> actor = NS::ActorFromJson(source, scene);
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(actor->ShadowClassObj(), nullptr);
    EXPECT_FLOAT_EQ(NS::FieldFloat(NS::SerializeClassObjFields(actor->ShadowClassObj()), "基本直径", 0.0f), 2.0f);
    const nlohmann::json saved = NS::ActorToJson(actor.get());
    EXPECT_FALSE(saved.contains("components"));
    ASSERT_TRUE(saved.contains("classObjs"));
    ASSERT_TRUE(saved["classObjs"].is_object());
    EXPECT_FLOAT_EQ(saved["classObjs"]["Shadow"]["基本直径"].get<float>(), 2.0f);
    EXPECT_FALSE(saved["classObjs"]["Shadow"].contains("id"));
    EXPECT_FALSE(saved["classObjs"]["Shadow"].contains("type"));
    EXPECT_FALSE(saved["classObjs"]["Shadow"].contains("fields"));
}

TEST(FixedParts, FileReferencesUseNamesAndClonesRemapIds)
{
    nlohmann::json scene = NS::MakeSceneJson();
    NS::SceneJsonActors(scene) = {
        {{"actor", "PropActor"}, {"id", 1u}, {"name", "target"}, {"classObjs", {{"Model", nlohmann::json::object()}}}},
        {{"actor", "PropActor"}, {"id", 2u}, {"classObjs", {{"Model", {{"reference", {{"ref", 1u}}}}}}}}};
    const std::string text = NS::SerializeSceneToJson(scene);
    const nlohmann::json file = nlohmann::json::parse(text);
    EXPECT_EQ(file["objects"][1]["classObjs"]["Model"]["reference"]["ref"], "target");
    nlohmann::json restored;
    ASSERT_TRUE(NS::DeserializeSceneFromJson(restored, text));
    nlohmann::json& copy = NS::SceneJsonActors(restored)[1];
    NS::RemapActorRefs(copy, {{1u, 9u}});
    EXPECT_EQ(copy["classObjs"]["Model"]["reference"], (nlohmann::json{{"ref", 9u}}));
}
