#include "Editor/EditorActors.h"
#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/SensorKinds.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/Actors/MapPartsActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"

#include <gtest/gtest.h>

// 各 SceneActor のクラスが、自分の部品を OnInit で組み立てることを縛る

TEST(ActorClass, MapPartsCollidesWithItsMesh)
{
    const std::unique_ptr<NS::MapPartsActor> partsOwned = NS::CreateActor<NS::MapPartsActor>();
    NS::MapPartsActor& parts = *partsOwned;
    const NS::Model* mesh = parts.ModelClassObj();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "cube");
    // 当たりは見た目のメッシュの三角形。箱や球の当たりは持たない
    EXPECT_NE(NS::Cast<NS::MeshCollision>(parts.CollisionClassObj()), nullptr);
    EXPECT_EQ(NS::Cast<NS::BoxCollision>(parts.CollisionClassObj()), nullptr);
    EXPECT_EQ(NS::Cast<NS::SphereCollision>(parts.CollisionClassObj()), nullptr);
}

TEST(ActorClass, MapObjOwnsMotionAndStaticCollision)
{
    const std::unique_ptr<GL::Level::PropActor> objOwned = NS::CreateActor<GL::Level::PropActor>();
    GL::Level::PropActor& obj = *objOwned;
    EXPECT_NE(obj.ModelClassObj(), nullptr);
    EXPECT_NE(NS::Cast<NS::SphereCollision>(obj.CollisionClassObj()), nullptr);
    const nlohmann::json prototype = NS::Editor::MakePrototypeJson(obj);
    EXPECT_EQ(NS::ClassObjFields(prototype, "Breakable"), nullptr);
    EXPECT_EQ(NS::ClassObjFields(prototype, "LaunchedBody"), nullptr);
    EXPECT_NE(obj.ShadowClassObj(), nullptr);
    // 体当たりは物の体のセンサーで調べられ、受け方と尾は自分で持つ
    const NS::HitSensor* sensor = obj.BodySensorClassObj();
    ASSERT_NE(sensor, nullptr);
    EXPECT_TRUE(GL::Level::IsSensorKind(*sensor, GL::Level::SensorKind::MapObjBody));
    EXPECT_EQ(NS::ClassObjFields(prototype, "TackleReaction"), nullptr);
    EXPECT_NE(NS::ClassObjFields(prototype, "LaunchEffects"), nullptr);
    EXPECT_NE(obj.GetStateMachine(), nullptr);
    EXPECT_FALSE(obj.IsFlying());
}

TEST(ActorClass, GoalHasMarkerAndArea)
{
    const std::unique_ptr<GL::Level::GoalActor> goalOwned = NS::CreateActor<GL::Level::GoalActor>();
    GL::Level::GoalActor& goal = *goalOwned;
    EXPECT_NE(goal.ModelClassObj(), nullptr);
    const NS::HitSensor* area = goal.BodySensorClassObj();
    ASSERT_NE(area, nullptr);
    EXPECT_TRUE(GL::Level::IsSensorKind(*area, GL::Level::SensorKind::Area));
}

TEST(ActorClass, DeathZoneIsBoxAreaWithoutTerrainCollision)
{
    const std::unique_ptr<GL::Level::DeathZoneActor> zoneOwned = NS::CreateActor<GL::Level::DeathZoneActor>();
    GL::Level::DeathZoneActor& zone = *zoneOwned;
    const NS::ShapeHitSensor* area = NS::Cast<NS::ShapeHitSensor>(zone.BodySensorClassObj());
    ASSERT_NE(area, nullptr);
    EXPECT_TRUE(GL::Level::IsSensorKind(*area, GL::Level::SensorKind::Area));
    EXPECT_EQ(area->Shape(), NS::HitSensorShape::Box);
    EXPECT_GE(area->BoxHalfExtents().x, 100.0f);
    // 地形の当たりを持つと、落ちてきたプレイヤーが上面に着地してしまう
    EXPECT_EQ(NS::Cast<NS::BoxCollision>(zone.CollisionClassObj()), nullptr);
}

TEST(ActorClass, FollowCameraHasFollowAndFeed)
{
    const std::unique_ptr<GL::Level::FollowCameraActor> cameraOwned = NS::CreateActor<GL::Level::FollowCameraActor>();
    GL::Level::FollowCameraActor& camera = *cameraOwned;
    EXPECT_NE(NS::ClassObjFields(NS::Editor::MakePrototypeJson(camera), "Vcam"), nullptr);
}

TEST(ActorClass, LightHasDirectionalLight)
{
    const std::unique_ptr<NS::LightActor> lightOwned = NS::CreateActor<NS::LightActor>();
    NS::LightActor& light = *lightOwned;
    EXPECT_NE(NS::ClassObjFields(NS::Editor::MakePrototypeJson(light), "DirectionalLight"), nullptr);
}

TEST(ActorClass, LightShinesAlongItsRotation)
{
    const std::unique_ptr<NS::LightActor> lightOwned = NS::CreateActor<NS::LightActor>();
    NS::LightActor& light = *lightOwned;
    const NS::DirectionalLight* sun = &light.Light();

    NS::Vector3 initial{-0.3f, -1.0f, -0.2f};
    initial.Normalize();
    EXPECT_NEAR(sun->Direction().x, initial.x, 1.0e-5f);
    EXPECT_NEAR(sun->Direction().y, initial.y, 1.0e-5f);
    EXPECT_NEAR(sun->Direction().z, initial.z, 1.0e-5f);

    const NS::Quaternion turned = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, 1.0f);
    light.Root().SetRotation(turned);
    const NS::Vector3 expected = NS::Vector3::Transform(NS::Vector3::UnitZ, turned);
    EXPECT_NEAR(sun->Direction().x, expected.x, 1.0e-5f);
    EXPECT_NEAR(sun->Direction().y, expected.y, 1.0e-5f);
    EXPECT_NEAR(sun->Direction().z, expected.z, 1.0e-5f);
}

TEST(ActorClass, PlayerBuildsWholeCompositionInInit)
{
    // 以前はシーンのデータが足していた体当たりと当たりの部品も、クラスが組み立てる
    const std::unique_ptr<PlayerActor> playerOwned = NS::CreateActor<PlayerActor>();
    PlayerActor& player = *playerOwned;
    const nlohmann::json prototype = NS::Editor::MakePrototypeJson(player);
    EXPECT_NE(NS::ClassObjFields(prototype, "Movement"), nullptr);
    EXPECT_NE(NS::ClassObjFields(prototype, "Input"), nullptr);
    EXPECT_EQ(NS::ClassObjFields(prototype, "PlayerInputRelay"), nullptr);
    EXPECT_NE(player.GetStateMachine(), nullptr);
    EXPECT_EQ(player.Phase(), NS::UpdatePhase::Player);
    EXPECT_NE(NS::ClassObjFields(prototype, "ImpactResolver"), nullptr);
    EXPECT_NE(player.HitReactionClassObj(), nullptr);
    const NS::HitSensor* body = player.BodySensorClassObj();
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(GL::Level::IsSensorKind(*body, GL::Level::SensorKind::PlayerBody));
}

TEST(SensorKinds, SetKindIsSeenByIsKindOnlyForThatKind)
{
    // 付けた種類にだけ真で、他の種類と未設定には偽
    NS::ShapeHitSensor sensor{nullptr};
    const GL::Level::SensorKind kinds[] = {
        GL::Level::SensorKind::PlayerBody, GL::Level::SensorKind::MapObjBody, GL::Level::SensorKind::Area};
    for (const GL::Level::SensorKind set : kinds)
    {
        GL::Level::SetSensorKind(sensor, set);
        for (const GL::Level::SensorKind asked : kinds)
        {
            EXPECT_EQ(GL::Level::IsSensorKind(sensor, asked), set == asked);
        }
        EXPECT_FALSE(GL::Level::IsSensorKind(sensor, GL::Level::SensorKind::Unset));
    }
}

TEST(SensorKinds, NewSensorIsUnsetAndUnsetCanBeWrittenBack)
{
    // 作ったばかりのセンサーはどの種類でもない。付け忘れたセンサーが黙って体当たりの相手にならない
    NS::ShapeHitSensor sensor{nullptr};
    EXPECT_EQ(sensor.Kind(), 0u);
    EXPECT_FALSE(GL::Level::IsSensorKind(sensor, GL::Level::SensorKind::MapObjBody));
    // 未設定を書くと未設定へ戻る
    GL::Level::SetSensorKind(sensor, GL::Level::SensorKind::Area);
    GL::Level::SetSensorKind(sensor, GL::Level::SensorKind::Unset);
    EXPECT_FALSE(GL::Level::IsSensorKind(sensor, GL::Level::SensorKind::Area));
    EXPECT_EQ(sensor.Kind(), 0u);
}
