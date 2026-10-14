#include "Game/Level/FollowCamera.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/Actors/Light.h"
#include "NSlib/Object/Actors/MapParts.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/PlayerInput.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"
#include "NSlib/Object/SubObjects/Transform.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace
{
    template <class T>
    constexpr bool k_CanCreatePartByName = requires(T& actor) { actor.CreateSubObj(std::string_view{"Model"}); };

    class DirectMemberActor final : public NS::Obj::Actor
    {
    public:
        std::unique_ptr<NS::Obj::Collision> shape;

    protected:
        void OnInit() override { (void)CreateSubObj<NS::Obj::Collision, NS::Obj::SphereCollision>(shape, "Shape"); }
        void VisitExtraSubObjs(const SubObjVisitor& visit) const override { VisitSubObjMembers(visit, shape); }

    public:
        template <class TSelf = DirectMemberActor> static void CheckTypes()
        {
            static_assert(requires(TSelf& self) {
                self.template CreateSubObj<NS::Obj::Collision, NS::Obj::SphereCollision>(self.shape, "Shape");
            });
            static_assert(!requires(TSelf& self) {
                self.template CreateSubObj<NS::Obj::Collision, NS::Obj::Model>(self.shape, "Shape");
            });
        }
    };

    class ModelPartActor final : public NS::Obj::Actor
    {
    protected:
        void OnInit() override { (void)CreateSubObj(m_model, "Model"); }
    };

    class SensorPartsActor final : public NS::Obj::Actor
    {
    protected:
        void OnInit() override
        {
            (void)CreateSubObj<NS::Obj::HitSensor, NS::Obj::ShapeHitSensor>(m_bodySensor, "BodySensor");
            (void)CreateSubObj<NS::Obj::HitSensor, NS::Obj::ShapeHitSensor>(m_attackSensor, "AttackSensor");
        }
    };

    class MemberOwnedPart final : public NS::Obj::SubObject
    {
    public:
        MemberOwnedPart(std::vector<std::string>& destroyed, std::string name)
            : m_destroyed(destroyed), m_name(std::move(name))
        {}

        ~MemberOwnedPart() override { m_destroyed.push_back(m_name); }
        void OnStart() override { ++starts; }
        void OnEndPlay() override { ++ends; }
        int starts = 0;
        int ends = 0;

    private:
        std::vector<std::string>& m_destroyed;
        std::string m_name;
    };

    class MemberOwnedActor final : public NS::Obj::Actor
    {
    public:
        explicit MemberOwnedActor(std::vector<std::string>& destroyed) : m_destroyed(destroyed) {}
        std::unique_ptr<MemberOwnedPart> first;
        std::unique_ptr<MemberOwnedPart> second;

        MemberOwnedPart* ReplaceFirst()
        {
            return CreateSubObj(first, "Replacement", m_destroyed, std::string{"replacement"});
        }

    protected:
        void OnInit() override
        {
            (void)CreateSubObj(first, "First", m_destroyed, std::string{"first"});
            (void)CreateSubObj(second, "Second", m_destroyed, std::string{"second"});
        }
        void VisitExtraSubObjs(const SubObjVisitor& visit) const override { VisitSubObjMembers(visit, first, second); }

    private:
        std::vector<std::string>& m_destroyed;
    };

    class PassivePart final : public NS::Obj::SubObject
    {
    public:
        void OnUpdate() override { ++updates; }
        int updates = 0;
    };

    class FixedPartActor final : public NS::Obj::Actor
    {
    public:
        std::unique_ptr<PassivePart> passive;

    protected:
        void VisitExtraSubObjs(const SubObjVisitor& visit) const override { VisitSubObjMembers(visit, passive); }
        void OnInit() override { (void)CreateSubObj(passive, "Passive"); }
    };

} // namespace

TEST(FixedParts, PartCreationRequiresAnOwningMember)
{
    EXPECT_FALSE(k_CanCreatePartByName<NS::Obj::Actor>);
}

TEST(FixedParts, MemberTypeCanOwnADerivedPartWithoutASlot)
{
    DirectMemberActor::CheckTypes();
    const std::unique_ptr<DirectMemberActor> actor = NS::Obj::CreateActor<DirectMemberActor>();
    ASSERT_NE(actor->shape, nullptr);
    EXPECT_NE(NS::Obj::Cast<NS::Obj::SphereCollision>(actor->shape.get()), nullptr);
    EXPECT_EQ(actor->shape->Owner(), actor.get());
    EXPECT_EQ(actor->FindSubObj("Shape"), actor->shape.get());
}

TEST(FixedParts, MapObjCreatesShadowBeforeArchetypeIsApplied)
{
    const std::unique_ptr<GL::Level::MapObj> actor = NS::Obj::CreateActor<GL::Level::MapObj>();
    ASSERT_NE(actor->ShadowSubObj(), nullptr);
    EXPECT_EQ(actor->FindSubObj("Shadow"), actor->ShadowSubObj());
}

TEST(FixedParts, MemberOwnershipSupportsLookupLifecycleAndDestruction)
{
    std::vector<std::string> destroyed;
    {
        const std::unique_ptr<MemberOwnedActor> owned = NS::Obj::CreateActor<MemberOwnedActor>(destroyed);
        MemberOwnedActor& actor = *owned;
        ASSERT_NE(actor.first, nullptr);
        ASSERT_NE(actor.second, nullptr);
        EXPECT_EQ(actor.first->Owner(), &actor);
        EXPECT_EQ(actor.FindSubObj("First"), actor.first.get());
        EXPECT_EQ(actor.FindSubObj("Second"), actor.second.get());
        EXPECT_EQ(actor.ReplaceFirst(), nullptr);
        EXPECT_EQ(actor.FindSubObj("First"), actor.first.get());
        EXPECT_EQ(actor.FindSubObj("Replacement"), nullptr);
        actor.OnStart();
        EXPECT_EQ(actor.first->starts, 1);
        EXPECT_EQ(actor.second->starts, 1);
        actor.OnEndPlay();
        EXPECT_EQ(actor.first->ends, 1);
        EXPECT_EQ(actor.second->ends, 1);
    }
    EXPECT_EQ(destroyed, (std::vector<std::string>{"second", "first"}));
}

TEST(FixedParts, TransformUsesTheSamePartLookupAndFieldEditing)
{
    NS::Obj::Actor actor;
    NS::Obj::Transform* transform = NS::Obj::Cast<NS::Obj::Transform>(actor.FindSubObj("Transform"));
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->Owner(), &actor);
    EXPECT_EQ(&actor.Root(), transform);
    NS::Obj::ApplyJsonFields(*transform, nlohmann::json{{"位置", {2.0f, 3.0f, 4.0f}}});
    EXPECT_EQ(actor.Root().Position(), (NS::Vector3{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(NS::Obj::ObjectToJson(actor)["subObjects"]["Transform"]["位置"],
              (nlohmann::json::array({2.0f, 3.0f, 4.0f})));
}

// 外から来る大きさだけを下限で止める。コードからの値は跡を 0 まで縮めるので止めない
TEST(FixedParts, TransformClampsOnlyTheScaleFromTheField)
{
    NS::Obj::Actor actor;
    NS::Obj::ApplyJsonFields(actor.Root(), nlohmann::json{{"スケール", {0.001f, -1.0f, 2.0f}}});
    EXPECT_EQ(actor.Root().Scale(), (NS::Vector3{0.01f, 0.01f, 2.0f}));

    actor.Root().SetScale(NS::Vector3{0.0f, 1.0f, 0.0f});
    EXPECT_EQ(actor.Root().Scale(), (NS::Vector3{0.0f, 1.0f, 0.0f}));
}

TEST(FixedParts, BaseActorDoesNotImplicitlyUpdateEveryEnumeratedPart)
{
    const std::unique_ptr<FixedPartActor> owned = NS::Obj::CreateActor<FixedPartActor>();
    FixedPartActor& actor = *owned;
    ASSERT_EQ(actor.FindSubObj("Passive"), actor.passive.get());
    actor.Update();
    EXPECT_EQ(actor.passive->updates, 0);
}

TEST(FixedParts, CommonMembersAreCreatedByTheActorClass)
{
    const NS::Obj::Actor empty;
    EXPECT_EQ(empty.ModelSubObj(), nullptr);
    const std::unique_ptr<ModelPartActor> owned = NS::Obj::CreateActor<ModelPartActor>();
    NS::Obj::Actor& actor = *owned;
    NS::Obj::Model* model = actor.ModelSubObj();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(actor.FindSubObj("Model"), model);
    EXPECT_EQ(model->Owner(), &actor);
    std::vector<std::string> names;
    actor.ForEachSubObj([&](const NS::Obj::SubObject* subObject) { names.push_back(subObject->Name()); });
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model"}));
}

TEST(FixedParts, SensorsHaveDistinctRoleNames)
{
    const std::unique_ptr<SensorPartsActor> owned = NS::Obj::CreateActor<SensorPartsActor>();
    NS::Obj::Actor& actor = *owned;
    NS::Obj::SubObject* body = actor.BodySensorSubObj();
    NS::Obj::SubObject* attack = actor.AttackSensorSubObj();
    ASSERT_NE(body, nullptr);
    ASSERT_NE(attack, nullptr);
    EXPECT_NE(body, attack);
    EXPECT_EQ(actor.FindSubObj("BodySensor"), body);
    EXPECT_EQ(actor.FindSubObj("AttackSensor"), attack);
    EXPECT_EQ(actor.FindSubObj("Missing"), nullptr);
    EXPECT_NE(NS::Obj::Cast<NS::Obj::ShapeHitSensor>(body), nullptr);
    EXPECT_NE(NS::Obj::Cast<NS::Obj::ShapeHitSensor>(attack), nullptr);
}

TEST(FixedParts, ConcreteActorsExposeTheirOwnedRoles)
{
    const std::unique_ptr<Player> ownedPlayer = NS::Obj::CreateActor<Player>();
    const std::unique_ptr<GL::Level::MapObj> ownedRock = NS::Obj::CreateActor<GL::Level::MapObj>();
    const std::unique_ptr<GL::Level::FollowCamera> ownedCamera = NS::Obj::CreateActor<GL::Level::FollowCamera>();
    const std::unique_ptr<NS::Obj::Light> ownedLight = NS::Obj::CreateActor<NS::Obj::Light>();
    const std::unique_ptr<NS::Obj::MapParts> ownedTerrain = NS::Obj::CreateActor<NS::Obj::MapParts>();
    Player& player = *ownedPlayer;
    GL::Level::MapObj& rock = *ownedRock;
    GL::Level::FollowCamera& camera = *ownedCamera;
    NS::Obj::Light& light = *ownedLight;
    NS::Obj::MapParts& terrain = *ownedTerrain;
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
    const nlohmann::json source = {{"class", "MapObj"}, {"id", 7u}, {"subObjects", {{"Collision", {{"半径", 2.0f}}}}}};
    std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(source, nullptr);
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(actor->CollisionSubObj(), nullptr);
    const NS::Obj::SphereCollision* sphere = NS::Obj::Cast<NS::Obj::SphereCollision>(actor->CollisionSubObj());
    ASSERT_NE(sphere, nullptr);
    EXPECT_FLOAT_EQ(sphere->Radius(), 2.0f);
    const nlohmann::json saved = NS::Obj::ObjectToJson(*actor);
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
    nlohmann::json scene = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(scene) = {
        {{"class", "MapObj"}, {"id", 1u}, {"name", "target"}, {"subObjects", {{"Model", nlohmann::json::object()}}}},
        {{"class", "MapObj"}, {"id", 2u}, {"subObjects", {{"Model", {{"reference", {{"ref", 1u}}}}}}}}};
    const std::string text = NS::Obj::SerializeSceneToJson(scene);
    const nlohmann::json file = nlohmann::json::parse(text);
    EXPECT_EQ(file["objects"][1]["subObjects"]["Model"]["reference"]["ref"], "target");
    nlohmann::json restored;
    ASSERT_TRUE(NS::Obj::DeserializeSceneFromJson(restored, text));
    nlohmann::json& copy = NS::Obj::SceneJsonObjects(restored)[1];
    NS::Obj::RemapObjectRefs(copy, {{1u, 9u}});
    EXPECT_EQ(copy["subObjects"]["Model"]["reference"], (nlohmann::json{{"ref", 9u}}));
}
