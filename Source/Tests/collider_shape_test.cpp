#include "Editor/EditorObjects.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <limits>

// 動く体の形 (半径・半分の高さ) は Collider が自分の欄に持つ。速度と接地の Body は寸法を持たない

namespace
{
    // PlayerActor を 1 体だけ置いた場面を読む。parts は個体の上書き
    void LoadPlayerScene(NS::Scene& scene, const nlohmann::json& parts)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(entry, "PlayerActor");
        NS::SetObjectJsonId(entry, 1);
        entry["classObjs"] = parts;
        NS::SceneJsonObjects(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
    }

    const NS::Collider* PlayerCollider(NS::Scene& scene)
    {
        const PlayerActor* player = static_cast<const PlayerActor*>(scene.Objects().FindByObjectId(1));
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
    NS::Collider collider{nullptr, nullptr};
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), 0.4f);
    EXPECT_FLOAT_EQ(collider.StandingHalfHeight(), 0.5f);
    EXPECT_EQ(collider.GetPhysicsScene(), nullptr);

    EXPECT_EQ(NS::ApplyJsonFields(collider, {{"半径", 0.3f}, {"半分の高さ", 0.7f}}), 0u);
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), 0.3f);
    EXPECT_FLOAT_EQ(collider.StandingHalfHeight(), 0.7f);
    EXPECT_FLOAT_EQ(collider.CapsuleHalfHeight(), 0.7f);
}

// 当たりの形は根を中心にした縦のカプセル。球にしている間は円柱の半分の高さが 0 になる
TEST(ColliderShape, CapsuleAtPutsTheCurrentShapeAroundTheRoot)
{
    NS::Collider collider{nullptr, nullptr};
    ASSERT_EQ(NS::ApplyJsonFields(collider, {{"半径", 0.3f}, {"半分の高さ", 0.7f}}), 0u);
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
    // 半径は 0 以下を、半長は負を受けない。半長 0 は球で、半径 0 は形を作れない
    NS::Collider collider{nullptr, nullptr};
    const float radiusBefore = collider.CapsuleRadius();
    collider.SetCapsuleRadius(0.0f);
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), radiusBefore);
    collider.SetCapsuleRadius(-1.0f);
    collider.SetStandingHalfHeight(-1.0f);
    EXPECT_FLOAT_EQ(collider.CapsuleRadius(), radiusBefore);
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
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    EXPECT_NE(NS::ClassObjFields(NS::Editor::MakePrototypeJson(player), "Collider"), nullptr);
}

TEST(ColliderShape, BodyHasNoShapeFields)
{
    // 寸法の正は Collider 1 つ。Movement に欄が残ると、同じ値の置き場が 2 つに割れる
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const NS::Object* movement = &player.Body();
    ASSERT_NE(movement->GetReflection(), nullptr);
    EXPECT_EQ(movement->GetReflection()->fieldCount, 0u);
}

TEST(ColliderShape, PlayerActorFileShapeLandsOnCollider)
{
    NS::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    const NS::Collider* collider = PlayerCollider(scene);
    ASSERT_NE(collider, nullptr);
    EXPECT_FLOAT_EQ(collider->CapsuleRadius(), 0.65f);
    EXPECT_FLOAT_EQ(collider->StandingHalfHeight(), 0.5f);
}

TEST(ColliderShape, PlayerBodySensorFollowsARadiusEditWithoutRestart)
{
    // インスペクタで寸法を変えたその場で、体のセンサーは移動と同じ形を返す。開始し直しを待たない
    NS::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    PlayerActor* player = static_cast<PlayerActor*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(player->Collider(), {{"半径", 0.8f}}), 0u);
    const NS::SensorVolume volume = player->BodySensorClassObj()->WorldVolume();
    EXPECT_FLOAT_EQ(volume.radius, 0.8f);
}

TEST(ColliderShape, ScaledPlayerRootDoesNotScaleTheBodySensor)
{
    // 移動の当たりは根のスケールに依らない。範囲が照合する体も同じ寸法のまま
    NS::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    PlayerActor* player = static_cast<PlayerActor*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);

    player->Root().SetScale(NS::Vector3{1.2f, 1.2f, 1.2f});
    const NS::SensorVolume volume = player->BodySensorClassObj()->WorldVolume();
    const NS::Phys::Capsule capsule = player->Collider().CapsuleAt(player->Root().Position());
    EXPECT_FLOAT_EQ(volume.radius, capsule.radius);
    EXPECT_FLOAT_EQ((volume.b - volume.a).Length(), capsule.halfHeight * 2.0f);
    EXPECT_FLOAT_EQ(volume.Center().y, player->Root().Position().y);
}

TEST(ColliderShape, MapObjBodySensorFollowsACollisionRadiusEdit)
{
    // 置物の体のセンサーは当たりの球をその場で映す。配置の後に半径を変えても割れない
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(rock, "PropActor");
    NS::SetObjectJsonId(rock, 1);
    NS::Editor::SetObjectPosition(rock, NS::Vector3{2.0f, 1.0f, 0.0f});
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    NS::SceneActor* placed = scene.Objects().FindByObjectId(1);
    ASSERT_NE(placed, nullptr);
    NS::SphereCollision* collision = NS::Cast<NS::SphereCollision>(placed->CollisionClassObj());
    ASSERT_NE(collision, nullptr);

    ASSERT_EQ(NS::ApplyJsonFields(*collision,
                                  {{"半径", 0.8f}, {"中心オフセット", {0.0f, 0.25f, 0.0f}}}),
              0u);
    const NS::Sphere sphere = collision->WorldSphere();
    const NS::SensorVolume volume = placed->BodySensorClassObj()->WorldVolume();
    EXPECT_FLOAT_EQ(sphere.radius, 0.8f);
    EXPECT_FLOAT_EQ(volume.radius, sphere.radius);
    EXPECT_FLOAT_EQ(volume.Center().x, sphere.center.x);
    EXPECT_FLOAT_EQ(volume.Center().y, sphere.center.y);
    EXPECT_FLOAT_EQ(volume.Center().z, sphere.center.z);
}

TEST(ColliderShape, FollowingBodySensorsShowNoFields)
{
    // 形の正は Collider と Collision。映すだけのセンサーに効かない欄を出さない
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const std::unique_ptr<GL::Level::PropActor> objOwned = NS::CreateActor<GL::Level::PropActor>();
    GL::Level::PropActor& obj = *objOwned;
    ASSERT_NE(player.BodySensorClassObj(), nullptr);
    ASSERT_NE(obj.BodySensorClassObj(), nullptr);
    EXPECT_EQ(player.BodySensorClassObj()->GetReflection()->fieldCount, 0u);
    EXPECT_EQ(obj.BodySensorClassObj()->GetReflection()->fieldCount, 0u);
}

TEST(ColliderShape, AreaSensorsKeepTheirSavedFieldNames)
{
    // ゴールと落下死の範囲は形を自分で持つ。保存済みの欄の表示名がそのまま読める
    const std::unique_ptr<GL::Level::GoalActor> goalOwned = NS::CreateActor<GL::Level::GoalActor>();
    GL::Level::GoalActor& goal = *goalOwned;
    const std::unique_ptr<GL::Level::DeathZoneActor> zoneOwned = NS::CreateActor<GL::Level::DeathZoneActor>();
    GL::Level::DeathZoneActor& zone = *zoneOwned;
    ASSERT_NE(goal.BodySensorClassObj(), nullptr);
    ASSERT_NE(zone.BodySensorClassObj(), nullptr);
    EXPECT_EQ(NS::ApplyJsonFields(*goal.BodySensorClassObj(), {{"半径", 0.9f}}), 0u);
    EXPECT_EQ(NS::ApplyJsonFields(*zone.BodySensorClassObj(),
                                  {{"箱の半径", {1000.0f, 5.0f, 1000.0f}}, {"中心オフセット", {0.0f, 0.0f, 0.0f}}}),
              0u);
}

TEST(ColliderShape, OldMovementShapeKeysRefuseThePlayer)
{
    // 古い場面に残る "Movement" の寸法は今の欄に無いので、自機を作らずに欄ごとの ERROR を出し、JSON のまま持つ
    NS::Scene scene;
    {
        const TestLogCapture log;
        LoadPlayerScene(scene, {{"Movement", {{"半径", 0.2f}, {"半分の高さ", 0.1f}}}});
        EXPECT_EQ(log.ErrorCount(), 2);
    }
    EXPECT_EQ(PlayerCollider(scene), nullptr);
    const nlohmann::json kept = scene.ToJson();
    ASSERT_EQ(NS::SceneJsonObjects(kept).size(), 1u);
    EXPECT_TRUE(NS::ObjectJsonClassObjs(NS::SceneJsonObjects(kept)[0])["Movement"].contains("半径"));
}

