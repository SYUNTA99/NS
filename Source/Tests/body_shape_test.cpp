#include "Game/Player.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/HitSensor.h"
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

TEST(BodyShape, BodySensorFollowsShapeEdits)
{
    // インスペクタで寸法を変えた時も、体のセンサーは移動と同じ形のまま
    NS::Obj::Scene scene;
    LoadPlayerScene(scene, nlohmann::json::object());
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    const NS::Obj::ShapeHitSensor* sensor = NS::Obj::ComponentCast<NS::Obj::ShapeHitSensor>(player->BodySensorPart());
    ASSERT_NE(sensor, nullptr);

    player->Body().SetCapsuleRadius(0.3f);
    player->Body().SetStandingHalfHeight(0.8f);
    EXPECT_FLOAT_EQ(sensor->Radius(), 0.3f);
    EXPECT_FLOAT_EQ(sensor->HalfHeight(), 0.8f);
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
