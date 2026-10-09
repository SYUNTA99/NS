#include "Game/Level/DeathZone.h"
#include "Game/Level/Goal.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/Collider.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"

#include <gtest/gtest.h>

#include <limits>

// 動く体の形 (半径・半分の高さ) は Collider が自分の欄に持つ。速度と接地の Body は寸法を持たない

namespace
{
    // Player を 1 体だけ置いた場面を読む。parts は個体の上書き
    void LoadPlayerScene(NS::Obj::Scene& scene, const nlohmann::json& parts)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json entry = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(entry, "Player");
        NS::Obj::SetObjectJsonId(entry, 1);
        entry["subObjects"] = parts;
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.LoadJson(doc);
    }

    const NS::Obj::Collider* PlayerCollider(NS::Obj::Scene& scene)
    {
        const Player* player = static_cast<const Player*>(scene.Objects().FindByObjectId(1));
        if (player == nullptr)
        {
            return nullptr;
        }
        return &player->Collider();
    }
} // namespace

TEST(ColliderShape, AnswersFromItsOwnFieldsWithoutOwner)
{
    // 持ち主も OnStart も無い当たりでも、既定と書いた値をそのまま返す
    NS::Obj::Collider collider;
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), 0.4f);
    EXPECT_FLOAT_EQ(collider.StandingHalfHeight(), 0.5f);
    EXPECT_EQ(collider.GetPhysicsScene(), nullptr);

    EXPECT_EQ(NS::Obj::ApplyJsonFields(collider, {{"半径", 0.3f}, {"半分の高さ", 0.7f}}), 0u);
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), 0.3f);
    EXPECT_FLOAT_EQ(collider.StandingHalfHeight(), 0.7f);
    EXPECT_FLOAT_EQ(collider.CapsuleHalfHeight(), 0.7f);
}

// 当たりの形は根を中心にした縦のカプセル。球にしている間は円柱の半分の高さが 0 になる
TEST(ColliderShape, CapsuleAtPutsTheCurrentShapeAroundTheRoot)
{
    NS::Obj::Collider collider;
    ASSERT_EQ(NS::Obj::ApplyJsonFields(collider, {{"半径", 0.3f}, {"半分の高さ", 0.7f}}), 0u);
    const NS::Vector3 root{1.0f, 2.0f, 3.0f};
    NS::Phys::Capsule capsule = collider.CapsuleAt(root);
    EXPECT_FLOAT_EQ(capsule.center.x, 1.0f);
    EXPECT_FLOAT_EQ(capsule.center.y, 2.0f);
    EXPECT_FLOAT_EQ(capsule.center.z, 3.0f);
    EXPECT_FLOAT_EQ(capsule.axis.x, 0.0f);
    EXPECT_FLOAT_EQ(capsule.axis.y, 1.0f);
    EXPECT_FLOAT_EQ(capsule.axis.z, 0.0f);
    EXPECT_FLOAT_EQ(capsule.halfHeight, 0.7f);
    EXPECT_FLOAT_EQ(capsule.radius, 0.3f);

    collider.SetSphereShape(true);
    capsule = collider.CapsuleAt(root);
    EXPECT_FLOAT_EQ(capsule.halfHeight, 0.0f);
    EXPECT_FLOAT_EQ(capsule.radius, 0.3f);
}

TEST(ColliderShape, ShapeFieldsRejectNegativeAndNonFinite)
{
    // 負は 0 にし、有限でない値は書く前の寸法を残す
    NS::Obj::Collider collider;
    collider.SetCapsuleRadius(-1.0f);
    collider.SetStandingHalfHeight(-1.0f);
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), 0.0f);
    EXPECT_FLOAT_EQ(collider.StandingHalfHeight(), 0.0f);

    collider.SetCapsuleRadius(0.3f);
    collider.SetStandingHalfHeight(0.7f);
    collider.SetCapsuleRadius(std::numeric_limits<float>::quiet_NaN());
    collider.SetStandingHalfHeight(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), 0.3f);
    EXPECT_FLOAT_EQ(collider.StandingHalfHeight(), 0.7f);
}

TEST(ColliderShape, PlayerOwnsAFixedColliderPart)
{
    // 動く体の当たりは自機の固定の部品 "Collider"。作り直しを頼んでも同じ部品を返す
    Player player;
    player.Init();
    EXPECT_EQ(player.FindSubObj("Collider"), &player.Collider());
    EXPECT_EQ(player.CreateSubObj("Collider"), &player.Collider());
}

TEST(ColliderShape, BodyHasNoShapeFields)
{
    // 寸法の正は Collider 1 つ。Movement に欄が残ると、同じ値の置き場が 2 つに割れる
    Player player;
    player.Init();
    const NS::Obj::SubObject* movement = player.FindSubObj("Movement");
    ASSERT_NE(movement, nullptr);
    ASSERT_NE(movement->GetReflection(), nullptr);
    EXPECT_EQ(movement->GetReflection()->fieldCount, 0u);
}

TEST(ColliderShape, PlayerArchetypeShapeLandsOnCollider)
{
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    const NS::Obj::Collider* collider = PlayerCollider(scene);
    ASSERT_NE(collider, nullptr);
    EXPECT_FLOAT_EQ(collider->CapsuleRadius(), 0.65f);
    EXPECT_FLOAT_EQ(collider->StandingHalfHeight(), 0.5f);
}

TEST(ColliderShape, PlayerBodySensorFollowsARadiusEditWithoutRestart)
{
    // インスペクタで寸法を変えたその場で、体のセンサーは移動と同じ形を返す。開始し直しを待たない
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    NS::Obj::SubObject* collider = player->FindSubObj("Collider");
    ASSERT_NE(collider, nullptr);

    ASSERT_EQ(NS::Obj::ApplyJsonFields(*collider, {{"半径", 0.8f}}), 0u);
    const NS::Obj::SensorVolume volume = player->BodySensorSubObj()->WorldVolume();
    EXPECT_FLOAT_EQ(volume.radius, 0.8f);
}

TEST(ColliderShape, ScaledPlayerRootDoesNotScaleTheBodySensor)
{
    // 移動の当たりは根のスケールに依らない。範囲が照合する体も同じ寸法のまま
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);

    player->Root().SetScale(NS::Vector3{1.2f, 1.2f, 1.2f});
    const NS::Obj::SensorVolume volume = player->BodySensorSubObj()->WorldVolume();
    const NS::Phys::Capsule capsule = player->Collider().CapsuleAt(player->Root().Position());
    EXPECT_FLOAT_EQ(volume.radius, capsule.radius);
    EXPECT_FLOAT_EQ((volume.b - volume.a).Length(), capsule.halfHeight * 2.0f);
    EXPECT_FLOAT_EQ(volume.Center().y, player->Root().Position().y);
}

TEST(ColliderShape, MapObjBodySensorFollowsACollisionRadiusEdit)
{
    // 置物の体のセンサーは当たりの球をその場で映す。配置の後に半径を変えても割れない
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 1);
    NS::Obj::SetObjectPosition(rock, NS::Vector3{2.0f, 1.0f, 0.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    NS::Obj::Actor* placed = scene.Objects().FindByObjectId(1);
    ASSERT_NE(placed, nullptr);
    const NS::Obj::SphereCollision* collision =
        NS::Obj::Cast<NS::Obj::SphereCollision>(placed->FindSubObj("Collision"));
    ASSERT_NE(collision, nullptr);

    ASSERT_EQ(
        NS::Obj::ApplyJsonFields(*placed->FindSubObj("Collision"), {{"半径", 0.8f}, {"中心オフセット", {0.0f, 0.25f, 0.0f}}}),
        0u);
    const NS::Sphere sphere = collision->WorldSphere();
    const NS::Obj::SensorVolume volume = placed->BodySensorSubObj()->WorldVolume();
    EXPECT_FLOAT_EQ(sphere.radius, 0.8f);
    EXPECT_FLOAT_EQ(volume.radius, sphere.radius);
    EXPECT_FLOAT_EQ(volume.Center().x, sphere.center.x);
    EXPECT_FLOAT_EQ(volume.Center().y, sphere.center.y);
    EXPECT_FLOAT_EQ(volume.Center().z, sphere.center.z);
}

TEST(ColliderShape, FollowingBodySensorsShowNoFields)
{
    // 形の正は Collider と Collision。映すだけのセンサーに効かない欄を出さない
    Player player;
    player.Init();
    NS::Game::Level::MapObj obj;
    obj.Init();
    ASSERT_NE(player.BodySensorSubObj(), nullptr);
    ASSERT_NE(obj.BodySensorSubObj(), nullptr);
    EXPECT_EQ(player.BodySensorSubObj()->GetReflection()->fieldCount, 0u);
    EXPECT_EQ(obj.BodySensorSubObj()->GetReflection()->fieldCount, 0u);
}

TEST(ColliderShape, AreaSensorsKeepTheirSavedFieldNames)
{
    // ゴールと落下死の範囲は形を自分で持つ。保存済みの欄の表示名がそのまま読める
    NS::Game::Level::Goal goal;
    goal.Init();
    NS::Game::Level::DeathZone zone;
    zone.Init();
    ASSERT_NE(goal.BodySensorSubObj(), nullptr);
    ASSERT_NE(zone.BodySensorSubObj(), nullptr);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(*goal.BodySensorSubObj(), {{"半径", 0.9f}}), 0u);
    EXPECT_EQ(
        NS::Obj::ApplyJsonFields(*zone.BodySensorSubObj(),
                                 {{"箱の半径", {1000.0f, 5.0f, 1000.0f}}, {"中心オフセット", {0.0f, 0.0f, 0.0f}}}),
        0u);
}

TEST(ColliderShape, ShapeSurvivesSaveAndReloadUnderCollider)
{
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, {{"Collider", {{"半径", 0.3f}, {"半分の高さ", 0.8f}}}});
    const nlohmann::json saved = scene.ToJson();

    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    const nlohmann::json& parts = NS::Obj::ObjectJsonSubObjs(objects[0]);
    ASSERT_TRUE(parts.contains("Collider"));
    EXPECT_FLOAT_EQ(parts["Collider"]["半径"].get<float>(), 0.3f);
    EXPECT_FLOAT_EQ(parts["Collider"]["半分の高さ"].get<float>(), 0.8f);

    scene.LoadJson(saved);
    const NS::Obj::Collider* collider = PlayerCollider(scene);
    ASSERT_NE(collider, nullptr);
    EXPECT_FLOAT_EQ(collider->CapsuleRadius(), 0.3f);
    EXPECT_FLOAT_EQ(collider->StandingHalfHeight(), 0.8f);
}

TEST(ColliderShape, OldMovementShapeKeysAreSkipped)
{
    // 古い場面に残る "Movement" の寸法は読み飛ばし、形は種類の既定の Collider から来る。保存し直すと消える
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, {{"Movement", {{"半径", 0.2f}, {"半分の高さ", 0.1f}}}});
    const NS::Obj::Collider* collider = PlayerCollider(scene);
    ASSERT_NE(collider, nullptr);
    EXPECT_FLOAT_EQ(collider->CapsuleRadius(), 0.65f);
    EXPECT_FLOAT_EQ(collider->StandingHalfHeight(), 0.5f);

    const nlohmann::json saved = scene.ToJson();
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(saved);
    ASSERT_EQ(objects.size(), 1u);
    const nlohmann::json& parts = NS::Obj::ObjectJsonSubObjs(objects[0]);
    if (parts.contains("Movement"))
    {
        EXPECT_FALSE(parts["Movement"].contains("半径"));
        EXPECT_FALSE(parts["Movement"].contains("半分の高さ"));
    }
}
