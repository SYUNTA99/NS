#include "Game/Level/CollisionInput.h"
#include "Game/Level/FollowCamera.h"
#include "Game/Level/Goal.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Runtime/Object/Actors/Light.h"
#include "Runtime/Object/Actors/MapParts.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/MeshCollider.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"

#include <gtest/gtest.h>

// 各 Actor のクラスが、自分の部品をコンストラクタで組み立てることを縛る
// 部品の組み立てはクラスだけが決めるので、ここが種類ごとの構成の唯一の出所になる

TEST(ActorClass, MapPartsCollidesWithItsMesh)
{
    const NS::Obj::MapParts parts;
    const NS::Obj::Model* mesh = parts.ModelPart();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "cube");
    // 当たりは見た目のメッシュの三角形。箱や球の当たりは持たない
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::MeshCollider>(parts.Part("Collision")), nullptr);
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::BoxCollider>(parts.Part("Collision")), nullptr);
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::SphereCollider>(parts.Part("Collision")), nullptr);
}

TEST(ActorClass, MapObjOwnsMotionAndStaticCollider)
{
    const NS::Game::Level::MapObj obj;
    EXPECT_NE(obj.ModelPart(), nullptr);
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::SphereCollider>(obj.Part("Collision")), nullptr);
    EXPECT_EQ(obj.Part("Breakable"), nullptr);
    EXPECT_EQ(obj.Part("LaunchedBody"), nullptr);
    // 影は種類の既定値が足す部品。コンストラクタは積まない
    EXPECT_EQ(obj.ShadowPart(), nullptr);
    // 体当たりは物の体のセンサーで調べられ、受け方と尾は自分で持つ
    const NS::Obj::HitSensor* sensor = obj.BodySensorPart();
    ASSERT_NE(sensor, nullptr);
    EXPECT_EQ(sensor->Type(), NS::Obj::HitSensorType::MapObjBody);
    EXPECT_EQ(obj.Part("TackleReaction"), nullptr);
    EXPECT_NE(NS::Obj::ComponentCast<NS::Game::Level::LaunchEffects>(obj.Part("LaunchEffects")), nullptr);
    EXPECT_NE(obj.GetStateMachine(), nullptr);
    EXPECT_FALSE(obj.IsFlying());
}

TEST(ActorClass, GoalHasMarkerAndArea)
{
    const NS::Game::Level::Goal goal;
    EXPECT_NE(goal.ModelPart(), nullptr);
    const NS::Obj::HitSensor* area = goal.BodySensorPart();
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->Type(), NS::Obj::HitSensorType::Area);
}

TEST(ActorClass, KillZoneIsBoxAreaWithoutTerrainCollision)
{
    const NS::Game::Level::KillZone zone;
    const NS::Obj::HitSensor* area = zone.BodySensorPart();
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->Type(), NS::Obj::HitSensorType::Area);
    EXPECT_EQ(area->Shape(), NS::Obj::HitSensorShape::Box);
    EXPECT_GE(area->BoxHalfExtents().x, 100.0f);
    // 地形の当たりを持つと、落ちてきたプレイヤーが上面に着地してしまう
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::BoxCollider>(zone.Part("Collision")), nullptr);
}

TEST(ActorClass, FollowCameraHasFollowAndFeed)
{
    const NS::Game::Level::FollowCamera camera;
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::ThirdPersonFollow>(camera.Part("Vcam")), nullptr);
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::ThirdPersonFollow>(camera.Part("Vcam")), &camera.Vcam());
}

TEST(ActorClass, LightHasDirectionalLight)
{
    const NS::Obj::Light light;
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::DirectionalLight>(light.Part("DirectionalLight")), nullptr);
}

TEST(ActorClass, PlayerBuildsWholeCompositionInConstructor)
{
    // 以前はシーンのデータが足していた体当たりと当たりの部品も、クラスが組み立てる
    const Player player;
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::Body>(player.Part("Movement")), nullptr);
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::PlayerInput>(player.Part("Input")), nullptr);
    EXPECT_EQ(player.Part("PlayerInputRelay"), nullptr);
    EXPECT_NE(player.GetStateMachine(), nullptr);
    EXPECT_EQ(player.Phase(), NS::Obj::UpdatePhase::Player);
    EXPECT_EQ(&player.Input(), NS::Obj::ComponentCast<NS::Obj::PlayerInput>(player.Part("Input")));
    EXPECT_EQ(&player.Body(), NS::Obj::ComponentCast<NS::Obj::Body>(player.Part("Movement")));
    EXPECT_NE(player.ColliderPart(), nullptr);
    EXPECT_NE(NS::Obj::ComponentCast<NS::Game::Level::CollisionInput>(player.Part("ChargeControl")), nullptr);
    EXPECT_NE(NS::Obj::ComponentCast<NS::Game::Level::ImpactResolver>(player.Part("ImpactResolver")), nullptr);
    EXPECT_NE(player.HitReactionPart(), nullptr);
    const NS::Obj::HitSensor* body = player.BodySensorPart();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Type(), NS::Obj::HitSensorType::PlayerBody);
}
