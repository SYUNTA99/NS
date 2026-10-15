#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/Actors/MapPartsActor.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Body.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/PlayerInput.h"
#include "NSlib/CoreObject/SubObjects/SphereCollision.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace
{
    template <class T> constexpr bool k_HasPartName = requires(const T& part) { part.Name(); };
    template <class T>
    constexpr bool k_HasReflectedParts = requires(const T& info) {
        info.subObjects;
        info.subObjectCount;
    };

    class MemberCreationProbeActor final : public NS::SceneActor
    {
    public:
        template <class TSelf = MemberCreationProbeActor> static constexpr bool CanCreateWithoutName()
        {
            return requires(TSelf& self) { self.CreateSubObj(self.part); };
        }

    private:
        std::unique_ptr<NS::Model> part;
    };

    template <class T>
    constexpr bool k_CanCreatePartByName = requires(T& actor) { actor.CreateSubObj(std::string_view{"Model"}); };

    class DirectMemberActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(DirectMemberActor, NS::SceneActor)
        NS_REFLECT_SUBOBJ(shape, "Shape")
        NS_REFLECT_END()

        std::unique_ptr<NS::Collision> shape;

    protected:
        void OnInit() override { CreateSubObj<NS::Collision, NS::SphereCollision>(shape); }

    public:
        template <class TSelf = DirectMemberActor> static void CheckTypes()
        {
            static_assert(
                requires(TSelf& self) { self.template CreateSubObj<NS::Collision, NS::SphereCollision>(self.shape); });
            static_assert(!requires(TSelf& self) { self.template CreateSubObj<NS::Collision, NS::Model>(self.shape); });
        }
    };

    class ModelPartActor final : public NS::SceneActor
    {
    protected:
        void OnInit() override { CreateSubObj(m_model); }
    };

    class SensorPartsActor final : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            CreateSubObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor);
            CreateSubObj<NS::HitSensor, NS::ShapeHitSensor>(m_attackSensor);
        }
    };

    class MemberOwnedPart final : public NS::SubObject
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
        NS_REFLECT_SUBOBJ(first, "First")
        NS_REFLECT_SUBOBJ(second, "Second")
        NS_REFLECT_END()

        explicit MemberOwnedActor(std::vector<std::string>& destroyed) : m_destroyed(destroyed) {}
        std::unique_ptr<MemberOwnedPart> first;
        std::unique_ptr<MemberOwnedPart> second;

        MemberOwnedPart* ReplaceFirst() { return CreateSubObj(first, m_destroyed, std::string{"replacement"}); }

    protected:
        void OnInit() override
        {
            CreateSubObj(first, m_destroyed, std::string{"first"});
            CreateSubObj(second, m_destroyed, std::string{"second"});
        }

    private:
        std::vector<std::string>& m_destroyed;
    };

    // 部品は自分からは動かない。基底に更新の関数があると、持ち主が全部の部品へ回す道が戻る
    template <class TSubObject = NS::SubObject> void CheckSubObjectHasNoUpdate()
    {
        static_assert(!requires(TSubObject& part) { part.OnUpdate(); });
    }

    template <class TSubObject = NS::SubObject> void CheckSubObjectHasNoOnOff()
    {
        static_assert(!requires(TSubObject& part) { part.SetActive(false); });
        static_assert(!requires(const TSubObject& part) { part.IsActive(); });
        static_assert(!requires(const TSubObject& part) { part.IsActiveSelf(); });
    }

    class ReflectedProbePart final : public NS::SubObject
    {
    public:
        float value = 0.0f;
        NS_REFLECT_BEGIN(ReflectedProbePart, NS::SubObject)
        NS_REFLECT_FIELD(value, "値")
        NS_REFLECT_END()
    };

    class ReflectedParentActor : public NS::SceneActor
    {
    public:
        std::unique_ptr<ReflectedProbePart> parentPart;
        NS_REFLECT_BEGIN(ReflectedParentActor, NS::SceneActor)
        NS_REFLECT_SUBOBJ(parentPart, "ParentPart")
        NS_REFLECT_END()

    protected:
        void OnInit() override { CreateSubObj(parentPart); }
    };

    class ReflectedHierarchyActor final : public ReflectedParentActor
    {
    public:
        std::unique_ptr<ReflectedProbePart> childPart;
        std::unique_ptr<ReflectedProbePart> absentPart;
        NS_REFLECT_BEGIN(ReflectedHierarchyActor, ReflectedParentActor)
        NS_REFLECT_SUBOBJ(childPart, "ChildPart")
        NS_REFLECT_SUBOBJ(absentPart, "AbsentPart")
        NS_REFLECT_END()

    protected:
        void OnInit() override
        {
            ReflectedParentActor::OnInit();
            CreateSubObj(childPart);
        }
    };
    NS_CLASS(ReflectedHierarchyActor)

    class UnregisteredPartActor final : public NS::SceneActor
    {
    public:
        std::unique_ptr<ReflectedProbePart> part;

    protected:
        void OnInit() override { CreateSubObj(part); }
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
        NS_REFLECT_SUBOBJ(part, "OffsetPart")
        NS_REFLECT_END()

    protected:
        void OnInit() override { CreateSubObj(part); }
    };

} // namespace

TEST(ReflectedParts, PartsDoNotStoreAnInstanceName)
{
    EXPECT_FALSE(k_HasPartName<NS::SubObject>);
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
    NS::ForEachReflectedSubObj(*actor, [&](const NS::SubObjectDesc& field, NS::SubObject* part) {
        keys.emplace_back(field.key);
        EXPECT_EQ(part->Owner(), actor.get());
    });
    EXPECT_EQ(keys, (std::vector<std::string>{"Transform", "ParentPart", "ChildPart"}));
    EXPECT_EQ(NS::Cast<ReflectedParentActor>(actor.get()), actor.get());
}

TEST(ReflectedParts, SameTypedMembersSaveAndReloadThroughTheirRegisteredKeys)
{
    const std::unique_ptr<ReflectedHierarchyActor> actor = NS::CreateActor<ReflectedHierarchyActor>();
    actor->parentPart->value = 2.0f;
    actor->childPart->value = 7.0f;
    const nlohmann::json saved = NS::ObjectToJson(*actor);
    EXPECT_FLOAT_EQ(saved["subObjects"]["ParentPart"]["値"].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(saved["subObjects"]["ChildPart"]["値"].get<float>(), 7.0f);
    EXPECT_FALSE(saved["subObjects"].contains("AbsentPart"));
    const std::unique_ptr<NS::SceneActor> loaded = NS::ObjectFromJson(saved, nullptr);
    const ReflectedHierarchyActor* restored = NS::Cast<ReflectedHierarchyActor>(loaded.get());
    ASSERT_NE(restored, nullptr);
    EXPECT_FLOAT_EQ(restored->parentPart->value, 2.0f);
    EXPECT_FLOAT_EQ(restored->childPart->value, 7.0f);
    EXPECT_EQ(restored->SubObjKey(*restored->parentPart), "ParentPart");
    EXPECT_EQ(restored->SubObjKey(*restored->childPart), "ChildPart");
    const ReflectedProbePart foreign;
    EXPECT_TRUE(restored->SubObjKey(foreign).empty());
}

TEST(ReflectedParts, UnregisteredMembersCannotCreateInvisibleParts)
{
    const std::unique_ptr<UnregisteredPartActor> actor = NS::CreateActor<UnregisteredPartActor>();
    EXPECT_EQ(actor->part, nullptr);
}

TEST(ReflectedParts, AccessorsAdjustTheActorBaseAddress)
{
    const std::unique_ptr<OffsetPartActor> actor = NS::CreateActor<OffsetPartActor>();
    ASSERT_NE(actor->part, nullptr);
    EXPECT_EQ(actor->FindSubObj("OffsetPart"), actor->part.get());
    EXPECT_EQ(actor->part->Owner(), static_cast<NS::SceneActor*>(actor.get()));
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
    EXPECT_EQ(actor->shape->Owner(), actor.get());
    EXPECT_EQ(actor->FindSubObj("Shape"), actor->shape.get());
}

TEST(FixedParts, MapObjCreatesShadowBeforeArchetypeIsApplied)
{
    const std::unique_ptr<GL::Level::PropActor> actor = NS::CreateActor<GL::Level::PropActor>();
    ASSERT_NE(actor->ShadowSubObj(), nullptr);
    EXPECT_EQ(actor->FindSubObj("Shadow"), actor->ShadowSubObj());
}

TEST(FixedParts, MemberOwnershipSupportsLookupAndDestruction)
{
    std::vector<std::string> destroyed;
    {
        const std::unique_ptr<MemberOwnedActor> owned = NS::CreateActor<MemberOwnedActor>(destroyed);
        MemberOwnedActor& actor = *owned;
        ASSERT_NE(actor.first, nullptr);
        ASSERT_NE(actor.second, nullptr);
        EXPECT_EQ(actor.first->Owner(), &actor);
        EXPECT_EQ(actor.FindSubObj("First"), actor.first.get());
        EXPECT_EQ(actor.FindSubObj("Second"), actor.second.get());
        EXPECT_EQ(actor.ReplaceFirst(), nullptr);
        EXPECT_EQ(actor.FindSubObj("First"), actor.first.get());
        EXPECT_EQ(actor.FindSubObj("Replacement"), nullptr);
    }
    EXPECT_EQ(destroyed, (std::vector<std::string>{"second", "first"}));
}

TEST(FixedParts, TransformUsesTheSamePartLookupAndFieldEditing)
{
    NS::SceneActor actor;
    NS::Transform* transform = NS::Cast<NS::Transform>(actor.FindSubObj("Transform"));
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->Owner(), &actor);
    EXPECT_EQ(&actor.Root(), transform);
    NS::ApplyJsonFields(*transform, nlohmann::json{{"位置", {2.0f, 3.0f, 4.0f}}});
    EXPECT_EQ(actor.Root().Position(), (NS::Vector3{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(NS::ObjectToJson(actor)["subObjects"]["Transform"]["位置"], (nlohmann::json::array({2.0f, 3.0f, 4.0f})));
}

// 外から来る大きさだけを下限で止める。コードからの値は跡を 0 まで縮めるので止めない
TEST(FixedParts, TransformClampsOnlyTheScaleFromTheField)
{
    NS::SceneActor actor;
    NS::ApplyJsonFields(actor.Root(), nlohmann::json{{"スケール", {0.001f, -1.0f, 2.0f}}});
    EXPECT_EQ(actor.Root().Scale(), (NS::Vector3{0.01f, 0.01f, 2.0f}));

    actor.Root().SetScale(NS::Vector3{0.0f, 1.0f, 0.0f});
    EXPECT_EQ(actor.Root().Scale(), (NS::Vector3{0.0f, 1.0f, 0.0f}));
}

TEST(FixedParts, SubObjectBaseHasNoUpdate)
{
    CheckSubObjectHasNoUpdate();
}

TEST(FixedParts, SubObjectBaseHasNoOnOff)
{
    CheckSubObjectHasNoOnOff();
}

TEST(FixedParts, CommonMembersAreCreatedByTheActorClass)
{
    const NS::SceneActor empty;
    EXPECT_EQ(empty.ModelSubObj(), nullptr);
    const std::unique_ptr<ModelPartActor> owned = NS::CreateActor<ModelPartActor>();
    NS::SceneActor& actor = *owned;
    NS::Model* model = actor.ModelSubObj();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(actor.FindSubObj("Model"), model);
    EXPECT_EQ(model->Owner(), &actor);
    std::vector<std::string> names;
    NS::ForEachReflectedSubObj(actor,
                               [&](const NS::SubObjectDesc& field, NS::SubObject*) { names.push_back(field.key); });
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model"}));
}

TEST(FixedParts, SensorsHaveDistinctRoleNames)
{
    const std::unique_ptr<SensorPartsActor> owned = NS::CreateActor<SensorPartsActor>();
    NS::SceneActor& actor = *owned;
    NS::SubObject* body = actor.BodySensorSubObj();
    NS::SubObject* attack = actor.AttackSensorSubObj();
    ASSERT_NE(body, nullptr);
    ASSERT_NE(attack, nullptr);
    EXPECT_NE(body, attack);
    EXPECT_EQ(actor.FindSubObj("BodySensor"), body);
    EXPECT_EQ(actor.FindSubObj("AttackSensor"), attack);
    EXPECT_EQ(actor.FindSubObj("Missing"), nullptr);
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
    EXPECT_EQ(player.FindSubObj("Input"), &player.Input());
    EXPECT_EQ(player.FindSubObj("Movement"), &player.Body());
    EXPECT_NE(player.FindSubObj("Params"), nullptr);
    EXPECT_NE(player.ModelSubObj(), nullptr);
    EXPECT_NE(player.BodySensorSubObj(), nullptr);
    EXPECT_NE(rock.ModelSubObj(), nullptr);
    EXPECT_NE(rock.CollisionSubObj(), nullptr);
    EXPECT_NE(rock.FindSubObj("Params"), nullptr);
    EXPECT_NE(rock.FindSubObj("LaunchEffects"), nullptr);
    EXPECT_EQ(camera.FindSubObj("Vcam"), &camera.Vcam());
    EXPECT_NE(light.FindSubObj("DirectionalLight"), nullptr);
    EXPECT_NE(terrain.ModelSubObj(), nullptr);
    EXPECT_NE(terrain.CollisionSubObj(), nullptr);
}

TEST(FixedParts, SavedObjectsUseRoleKeysAndDirectFields)
{
    const nlohmann::json source = {
        {"class", "PropActor"}, {"id", 7u}, {"subObjects", {{"Collision", {{"半径", 2.0f}}}}}};
    std::unique_ptr<NS::SceneActor> actor = NS::ObjectFromJson(source, nullptr);
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(actor->CollisionSubObj(), nullptr);
    const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionSubObj());
    ASSERT_NE(sphere, nullptr);
    EXPECT_FLOAT_EQ(sphere->Radius(), 2.0f);
    const nlohmann::json saved = NS::ObjectToJson(*actor);
    EXPECT_FALSE(saved.contains("components"));
    ASSERT_TRUE(saved.contains("subObjects"));
    ASSERT_TRUE(saved["subObjects"].is_object());
    EXPECT_FLOAT_EQ(saved["subObjects"]["Collision"]["半径"].get<float>(), 2.0f);
    EXPECT_FALSE(saved["subObjects"]["Collision"].contains("id"));
    EXPECT_FALSE(saved["subObjects"]["Collision"].contains("type"));
    EXPECT_FALSE(saved["subObjects"]["Collision"].contains("fields"));
    EXPECT_TRUE(saved["subObjects"]["Transform"].contains("位置"));
}

TEST(FixedParts, FileReferencesUseNamesAndClonesRemapIds)
{
    nlohmann::json scene = NS::MakeSceneJson();
    NS::SceneJsonObjects(scene) = {
        {{"class", "PropActor"}, {"id", 1u}, {"name", "target"}, {"subObjects", {{"Model", nlohmann::json::object()}}}},
        {{"class", "PropActor"}, {"id", 2u}, {"subObjects", {{"Model", {{"reference", {{"ref", 1u}}}}}}}}};
    const std::string text = NS::SerializeSceneToJson(scene);
    const nlohmann::json file = nlohmann::json::parse(text);
    EXPECT_EQ(file["objects"][1]["subObjects"]["Model"]["reference"]["ref"], "target");
    nlohmann::json restored;
    ASSERT_TRUE(NS::DeserializeSceneFromJson(restored, text));
    nlohmann::json& copy = NS::SceneJsonObjects(restored)[1];
    NS::RemapObjectRefs(copy, {{1u, 9u}});
    EXPECT_EQ(copy["subObjects"]["Model"]["reference"], (nlohmann::json{{"ref", 9u}}));
}
