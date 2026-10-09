#include "Game/Level/FollowCamera.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Actors/Light.h"
#include "NSlib/Object/Actors/MapParts.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/PlayerInput.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    class PassivePart final : public NS::Obj::SubObject
    {
    public:
        void OnUpdate() override { ++updates; }
        int updates = 0;
    };

    class FixedPartActor final : public NS::Obj::Actor
    {
    public:
        PassivePart* passive = nullptr;

    protected:
        void OnInit() override { passive = CreateSubObj<PassivePart>("Passive"); }
    };

} // namespace

TEST(FixedParts, BaseActorDoesNotImplicitlyUpdateEveryEnumeratedPart)
{
    FixedPartActor actor;
    actor.Init();
    ASSERT_EQ(actor.FindSubObj("Passive"), actor.passive);
    actor.Update();
    EXPECT_EQ(actor.passive->updates, 0);
}

TEST(FixedParts, CommonSlotsAreOptionalAndCreationIsIdempotent)
{
    NS::Obj::Actor actor;
    EXPECT_EQ(actor.ModelSubObj(), nullptr);
    NS::Obj::SubObject* created = actor.CreateSubObj("Model");
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created, actor.ModelSubObj());
    EXPECT_EQ(created, actor.CreateSubObj("Model"));
    EXPECT_EQ(created->Owner(), &actor);
    EXPECT_EQ(actor.CreateSubObj("UnknownPart"), nullptr);
    std::vector<std::string> names;
    for (const NS::Obj::SubObject* subObject : actor.SubObjs())
    {
        names.push_back(subObject->Name());
    }
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model"}));
}

TEST(FixedParts, SensorsHaveDistinctRoleNames)
{
    NS::Obj::Actor actor;
    NS::Obj::SubObject* body = actor.CreateSubObj("BodySensor");
    NS::Obj::SubObject* attack = actor.CreateSubObj("AttackSensor");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(attack, nullptr);
    EXPECT_NE(body, attack);
    EXPECT_EQ(actor.FindSubObj("BodySensor"), body);
    EXPECT_EQ(actor.FindSubObj("AttackSensor"), attack);
    EXPECT_EQ(actor.FindSubObj("Missing"), nullptr);
    // どちらも形を自分で持つセンサー
    EXPECT_NE(NS::Obj::Cast<NS::Obj::ShapeHitSensor>(body), nullptr);
    EXPECT_NE(NS::Obj::Cast<NS::Obj::ShapeHitSensor>(attack), nullptr);
}

TEST(FixedParts, ConcreteActorsExposeTheirOwnedRoles)
{
    Player player;
    player.Init();
    GL::Level::MapObj rock;
    rock.Init();
    GL::Level::FollowCamera camera;
    camera.Init();
    NS::Obj::Light light;
    light.Init();
    NS::Obj::MapParts terrain;
    terrain.Init();
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
