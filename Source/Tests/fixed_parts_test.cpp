#include "Game/Level/FollowCamera.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Actors/Light.h"
#include "Runtime/Object/Actors/MapParts.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    class PassivePart final : public NS::Obj::Component
    {
    public:
        void OnUpdate() override { ++updates; }
        int updates = 0;
    };

    class FixedPartActor final : public NS::Obj::Actor
    {
    public:
        FixedPartActor() { AttachFixedComponent(passive); }
        void ForEachPart(const PartVisitor& visitor) const override
        {
            NS::Obj::Actor::ForEachPart(visitor);
            visitor("Passive", passive);
        }
        mutable PassivePart passive;
    };

} // namespace

TEST(FixedParts, BaseActorDoesNotImplicitlyUpdateEveryEnumeratedPart)
{
    FixedPartActor actor;
    ASSERT_EQ(actor.Part("Passive"), &actor.passive);
    actor.Update();
    EXPECT_EQ(actor.passive.updates, 0);
}

TEST(FixedParts, CommonSlotsAreOptionalAndCreationIsIdempotent)
{
    NS::Obj::Actor actor;
    EXPECT_EQ(actor.ModelPart(), nullptr);
    NS::Obj::Component* created = actor.CreatePart("Model");
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created, actor.ModelPart());
    EXPECT_EQ(created, actor.CreatePart("Model"));
    EXPECT_EQ(created->Owner(), &actor);
    EXPECT_EQ(actor.CreatePart("UnknownPart"), nullptr);
    std::vector<std::string> names;
    actor.ForEachPart([&names](std::string_view name, NS::Obj::Component&) { names.emplace_back(name); });
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model"}));
}

TEST(FixedParts, SensorsHaveDistinctRoleNames)
{
    NS::Obj::Actor actor;
    NS::Obj::Component* body = actor.CreatePart("BodySensor");
    NS::Obj::Component* attack = actor.CreatePart("AttackSensor");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(attack, nullptr);
    EXPECT_NE(body, attack);
    EXPECT_EQ(actor.Part("BodySensor"), body);
    EXPECT_EQ(actor.Part("AttackSensor"), attack);
    EXPECT_EQ(actor.Part("Missing"), nullptr);
    // どちらも形を自分で持つセンサー
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::ShapeHitSensor>(body), nullptr);
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::ShapeHitSensor>(attack), nullptr);
}

TEST(FixedParts, ConcreteActorsExposeTheirOwnedRoles)
{
    Player player;
    NS::Game::Level::MapObj rock;
    NS::Game::Level::FollowCamera camera;
    NS::Obj::Light light;
    NS::Obj::MapParts terrain;
    EXPECT_EQ(player.Part("Input"), &player.Input());
    EXPECT_EQ(player.Part("Movement"), &player.Body());
    EXPECT_NE(player.Part("Params"), nullptr);
    EXPECT_NE(player.ModelPart(), nullptr);
    EXPECT_NE(player.BodySensorPart(), nullptr);
    EXPECT_NE(rock.ModelPart(), nullptr);
    EXPECT_NE(rock.CollisionPart(), nullptr);
    EXPECT_NE(rock.Part("Params"), nullptr);
    EXPECT_NE(rock.Part("LaunchEffects"), nullptr);
    EXPECT_EQ(camera.Part("Vcam"), &camera.Vcam());
    EXPECT_NE(light.Part("DirectionalLight"), nullptr);
    EXPECT_NE(terrain.ModelPart(), nullptr);
    EXPECT_NE(terrain.CollisionPart(), nullptr);
}

TEST(FixedParts, SavedObjectsUseRoleKeysAndDirectFields)
{
    const nlohmann::json source = {{"class", "MapObj"}, {"id", 7u}, {"parts", {{"Collision", {{"半径", 2.0f}}}}}};
    std::unique_ptr<NS::Obj::Actor> actor = NS::Obj::ObjectFromJson(source, nullptr);
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(actor->CollisionPart(), nullptr);
    const NS::Obj::SphereCollision* sphere = NS::Obj::ComponentCast<NS::Obj::SphereCollision>(actor->CollisionPart());
    ASSERT_NE(sphere, nullptr);
    EXPECT_FLOAT_EQ(sphere->Radius(), 2.0f);
    const nlohmann::json saved = NS::Obj::ObjectToJson(*actor);
    EXPECT_FALSE(saved.contains("components"));
    ASSERT_TRUE(saved.contains("parts"));
    ASSERT_TRUE(saved["parts"].is_object());
    EXPECT_FLOAT_EQ(saved["parts"]["Collision"]["半径"].get<float>(), 2.0f);
    EXPECT_FALSE(saved["parts"]["Collision"].contains("id"));
    EXPECT_FALSE(saved["parts"]["Collision"].contains("type"));
    EXPECT_FALSE(saved["parts"]["Collision"].contains("fields"));
    EXPECT_TRUE(saved["parts"]["Transform"].contains("位置"));
}

TEST(FixedParts, FileReferencesAndClonesPreservePartRoles)
{
    nlohmann::json scene = NS::Obj::MakeSceneJson();
    NS::Obj::SceneJsonObjects(scene) = {
        {{"class", "MapObj"}, {"id", 1u}, {"name", "target"}, {"parts", {{"Model", nlohmann::json::object()}}}},
        {{"class", "MapObj"}, {"id", 2u}, {"parts", {{"Model", {{"reference", {{"ref", 1u}, {"part", "Model"}}}}}}}}};
    const std::string text = NS::Obj::SerializeSceneToJson(scene);
    const nlohmann::json file = nlohmann::json::parse(text);
    EXPECT_EQ(file["objects"][1]["parts"]["Model"]["reference"]["ref"], "target");
    nlohmann::json restored;
    ASSERT_TRUE(NS::Obj::DeserializeSceneFromJson(restored, text));
    nlohmann::json& copy = NS::Obj::SceneJsonObjects(restored)[1];
    NS::Obj::RemapObjectRefs(copy, {{1u, 9u}});
    EXPECT_EQ(copy["parts"]["Model"]["reference"], (nlohmann::json{{"ref", 9u}, {"part", "Model"}}));
}
