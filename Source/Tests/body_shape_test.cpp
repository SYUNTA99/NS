#include "Game/Level/Goal.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <gtest/gtest.h>

#include <limits>

// 動く体の形 (半径・半分の高さ) は Body が自分の欄に持つ。同じ Actor の当たりの部品から借りない

namespace
{
    // Player を 1 体だけ置いた場面を読む。parts は個体の上書き
    void LoadPlayerScene(NS::Obj::Scene& scene, const nlohmann::json& parts)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        entry["parts"] = parts;
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.LoadJson(doc);
    }

    const NS::Obj::Body* PlayerBody(NS::Obj::Scene& scene)
    {
        const Player* player = static_cast<const Player*>(scene.Objects().FindByObjectId(1));
        if (player == nullptr)
        {
            return nullptr;
        }
        return &player->Body();
    }
} // namespace

TEST(BodyShape, AnswersFromItsOwnFieldsWithoutOwner)
{
    // 持ち主も OnStart も無い体でも、既定と書いた値をそのまま返す
    NS::Obj::Body body;
    EXPECT_FLOAT_EQ(body.CapsuleRadius(), 0.4f);
    EXPECT_FLOAT_EQ(body.StandingHalfHeight(), 0.5f);

    EXPECT_EQ(NS::Obj::ApplyJsonFields(body, {{"半径", 0.3f}, {"半分の高さ", 0.7f}}), 0u);
    EXPECT_FLOAT_EQ(body.CapsuleRadius(), 0.3f);
    EXPECT_FLOAT_EQ(body.StandingHalfHeight(), 0.7f);
    EXPECT_FLOAT_EQ(body.CapsuleHalfHeight(), 0.7f);
}

// 当たりの形は根を中心にした縦のカプセル。球にしている間は円柱の半分の高さが 0 になる
TEST(BodyShape, CapsuleAtPutsTheCurrentShapeAroundTheRoot)
{
    NS::Obj::Body body;
    ASSERT_EQ(NS::Obj::ApplyJsonFields(body, {{"半径", 0.3f}, {"半分の高さ", 0.7f}}), 0u);
    const NS::Core::Vector3 root{1.0f, 2.0f, 3.0f};
    NS::Phys::Capsule capsule = body.CapsuleAt(root);
    EXPECT_FLOAT_EQ(capsule.center.x, 1.0f);
    EXPECT_FLOAT_EQ(capsule.center.y, 2.0f);
    EXPECT_FLOAT_EQ(capsule.center.z, 3.0f);
    EXPECT_FLOAT_EQ(capsule.axis.x, 0.0f);
    EXPECT_FLOAT_EQ(capsule.axis.y, 1.0f);
    EXPECT_FLOAT_EQ(capsule.axis.z, 0.0f);
    EXPECT_FLOAT_EQ(capsule.halfHeight, 0.7f);
    EXPECT_FLOAT_EQ(capsule.radius, 0.3f);

    body.SetSphereShape(true);
    capsule = body.CapsuleAt(root);
    EXPECT_FLOAT_EQ(capsule.halfHeight, 0.0f);
    EXPECT_FLOAT_EQ(capsule.radius, 0.3f);
}

TEST(BodyShape, ShapeFieldsRejectNegativeAndNonFinite)
{
    // 負は 0 にし、有限でない値は書く前の寸法を残す
    NS::Obj::Body body;
    body.SetCapsuleRadius(-1.0f);
    body.SetStandingHalfHeight(-1.0f);
    EXPECT_FLOAT_EQ(body.CapsuleRadius(), 0.0f);
    EXPECT_FLOAT_EQ(body.StandingHalfHeight(), 0.0f);

    body.SetCapsuleRadius(0.3f);
    body.SetStandingHalfHeight(0.7f);
    body.SetCapsuleRadius(std::numeric_limits<float>::quiet_NaN());
    body.SetStandingHalfHeight(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(body.CapsuleRadius(), 0.3f);
    EXPECT_FLOAT_EQ(body.StandingHalfHeight(), 0.7f);
}

TEST(BodyShape, PlayerHasNoColliderPart)
{
    // 体の形の置き場だった部品 "Collider" は、Player にも基底の Actor にも無い
    Player player;
    EXPECT_EQ(player.Part("Collider"), nullptr);
    EXPECT_EQ(player.CreatePart("Collider"), nullptr);
}

TEST(BodyShape, PlayerArchetypeShapeLandsOnBody)
{
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    const NS::Obj::Body* body = PlayerBody(scene);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(body->CapsuleRadius(), 0.65f);
    EXPECT_FLOAT_EQ(body->StandingHalfHeight(), 0.5f);
}

TEST(BodyShape, PlayerBodySensorFollowsARadiusEditWithoutRestart)
{
    // インスペクタで寸法を変えたその場で、体のセンサーは移動と同じ形を返す。開始し直しを待たない
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    NS::Obj::Component* movement = player->Part("Movement");
    ASSERT_NE(movement, nullptr);

    ASSERT_EQ(NS::Obj::ApplyJsonFields(*movement, {{"半径", 0.8f}}), 0u);
    const NS::Obj::SensorVolume volume = player->BodySensorPart()->WorldVolume();
    EXPECT_FLOAT_EQ(volume.radius, 0.8f);
}

TEST(BodyShape, ScaledPlayerRootDoesNotScaleTheBodySensor)
{
    // 移動の当たりは根のスケールに依らない。範囲が照合する体も同じ寸法のまま
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);

    player->Root().SetScale(NS::Core::Vector3{1.2f, 1.2f, 1.2f});
    const NS::Obj::SensorVolume volume = player->BodySensorPart()->WorldVolume();
    const NS::Phys::Capsule capsule = player->Body().CapsuleAt(player->Root().Position());
    EXPECT_FLOAT_EQ(volume.radius, capsule.radius);
    EXPECT_FLOAT_EQ((volume.b - volume.a).Length(), capsule.halfHeight * 2.0f);
    EXPECT_FLOAT_EQ(volume.Center().y, player->Root().Position().y);
}

TEST(BodyShape, MapObjBodySensorFollowsACollisionRadiusEdit)
{
    // 置物の体のセンサーは当たりの球をその場で映す。配置の後に半径を変えても割れない
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 1);
    NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{2.0f, 1.0f, 0.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    NS::Obj::Actor* placed = scene.Objects().FindByObjectId(1);
    ASSERT_NE(placed, nullptr);
    const NS::Obj::SphereCollider* collision =
        NS::Obj::ComponentCast<NS::Obj::SphereCollider>(placed->Part("Collision"));
    ASSERT_NE(collision, nullptr);

    ASSERT_EQ(
        NS::Obj::ApplyJsonFields(*placed->Part("Collision"), {{"半径", 0.8f}, {"中心オフセット", {0.0f, 0.25f, 0.0f}}}),
        0u);
    const NS::Core::Sphere sphere = collision->WorldSphere();
    const NS::Obj::SensorVolume volume = placed->BodySensorPart()->WorldVolume();
    EXPECT_FLOAT_EQ(sphere.radius, 0.8f);
    EXPECT_FLOAT_EQ(volume.radius, sphere.radius);
    EXPECT_FLOAT_EQ(volume.Center().x, sphere.center.x);
    EXPECT_FLOAT_EQ(volume.Center().y, sphere.center.y);
    EXPECT_FLOAT_EQ(volume.Center().z, sphere.center.z);
}

TEST(BodyShape, FollowingBodySensorsShowNoFields)
{
    // 形の正は Movement と Collision。映すだけのセンサーに効かない欄を出さない
    const Player player;
    const NS::Game::Level::MapObj obj;
    ASSERT_NE(player.BodySensorPart(), nullptr);
    ASSERT_NE(obj.BodySensorPart(), nullptr);
    EXPECT_EQ(player.BodySensorPart()->GetReflection()->fieldCount, 0u);
    EXPECT_EQ(obj.BodySensorPart()->GetReflection()->fieldCount, 0u);
}

TEST(BodyShape, AreaSensorsKeepTheirSavedFieldNames)
{
    // ゴールと落下死の範囲は形を自分で持つ。保存済みの欄の表示名がそのまま読める
    NS::Game::Level::Goal goal;
    NS::Game::Level::KillZone zone;
    ASSERT_NE(goal.BodySensorPart(), nullptr);
    ASSERT_NE(zone.BodySensorPart(), nullptr);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(*goal.BodySensorPart(), {{"半径", 0.9f}}), 0u);
    EXPECT_EQ(
        NS::Obj::ApplyJsonFields(*zone.BodySensorPart(),
                                 {{"箱の半径", {1000.0f, 5.0f, 1000.0f}}, {"中心オフセット", {0.0f, 0.0f, 0.0f}}}),
        0u);
}

TEST(BodyShape, ShapeSurvivesSaveAndReload)
{
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, {{"Movement", {{"半径", 0.3f}, {"半分の高さ", 0.8f}}}});
    const nlohmann::json saved = scene.ToJson();
    scene.LoadJson(saved);
    const NS::Obj::Body* body = PlayerBody(scene);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(body->CapsuleRadius(), 0.3f);
    EXPECT_FLOAT_EQ(body->StandingHalfHeight(), 0.8f);
}

TEST(BodyShape, RemovedColliderKeyIsSkippedAndShapeStaysOnBody)
{
    // 古い場面に残る "Collider" の値は読み飛ばし、体の形は種類の既定の Movement から来る
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, {{"Collider", {{"半径", 0.2f}, {"半分の高さ", 0.1f}}}});
    const NS::Obj::Body* body = PlayerBody(scene);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(body->CapsuleRadius(), 0.65f);
    EXPECT_FLOAT_EQ(body->StandingHalfHeight(), 0.5f);

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_FALSE(NS::Obj::ObjectJsonParts(objects[0]).contains("Collider"));
}
