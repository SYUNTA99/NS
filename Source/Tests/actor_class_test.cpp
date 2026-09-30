#include "Game/Level/Breakable.h"
#include "Game/Level/CollisionInput.h"
#include "Game/Level/FollowCamera.h"
#include "Game/Level/FollowCameraFeed.h"
#include "Game/Level/Goal.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Level/MapObj.h"
#include "Game/Level/TackleReaction.h"
#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Actors/Light.h"
#include "Runtime/Object/Actors/MapParts.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/MeshCollider.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"

#include <gtest/gtest.h>

// 各 Actor のクラスが、自分の部品をコンストラクタで組み立てることを縛る
// 部品の組み立てはクラスだけが決めるので、ここが種類ごとの構成の唯一の出所になる

TEST(ActorClass, MapPartsCollidesWithItsMesh)
{
    const NS::Obj::MapParts parts;
    const NS::Obj::MeshRenderer* mesh = parts.FindComponent<NS::Obj::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "cube");
    // 当たりは見た目のメッシュの三角形。箱や球の当たりは持たない
    EXPECT_NE(parts.FindComponent<NS::Obj::MeshCollider>(), nullptr);
    EXPECT_EQ(parts.FindComponent<NS::Obj::BoxCollider>(), nullptr);
    EXPECT_EQ(parts.FindComponent<NS::Obj::SphereCollider>(), nullptr);
}

TEST(ActorClass, MapObjIsKinematicLaunchableBody)
{
    const NS::Game::Level::MapObj obj;
    EXPECT_NE(obj.FindComponent<NS::Obj::MeshRenderer>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Obj::SphereCollider>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Game::Level::Breakable>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Game::Level::LaunchedBody>(), nullptr);
    // 影は種類の既定値が足す部品。コンストラクタは積まない
    EXPECT_EQ(obj.FindComponent<NS::Obj::Shadow>(), nullptr);
    // 体当たりは物の体のセンサーで調べられ、受け方と尾は自分で持つ
    const NS::Obj::HitSensor* sensor = obj.FindComponent<NS::Obj::HitSensor>();
    ASSERT_NE(sensor, nullptr);
    EXPECT_EQ(sensor->Type(), NS::Obj::HitSensorType::MapObjBody);
    EXPECT_NE(obj.FindComponent<NS::Game::Level::TackleReaction>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Game::Level::LaunchEffects>(), nullptr);
    const NS::Obj::RigidBody* body = obj.FindComponent<NS::Obj::RigidBody>();
    ASSERT_NE(body, nullptr);
    // 置かれている間は動かない
    EXPECT_TRUE(body->IsKinematic());
}

TEST(ActorClass, GoalHasMarkerAndArea)
{
    const NS::Game::Level::Goal goal;
    EXPECT_NE(goal.FindComponent<NS::Obj::MeshRenderer>(), nullptr);
    const NS::Obj::HitSensor* area = goal.FindComponent<NS::Obj::HitSensor>();
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->Type(), NS::Obj::HitSensorType::Area);
}

TEST(ActorClass, KillZoneIsBoxAreaWithoutTerrainCollision)
{
    const NS::Game::Level::KillZone zone;
    const NS::Obj::HitSensor* area = zone.FindComponent<NS::Obj::HitSensor>();
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->Type(), NS::Obj::HitSensorType::Area);
    EXPECT_EQ(area->Shape(), NS::Obj::HitSensorShape::Box);
    EXPECT_GE(area->BoxHalfExtents().x, 100.0f);
    // 地形の当たりを持つと、落ちてきたプレイヤーが上面に着地してしまう
    EXPECT_EQ(zone.FindComponent<NS::Obj::BoxCollider>(), nullptr);
}

TEST(ActorClass, FollowCameraHasFollowAndFeed)
{
    const NS::Game::Level::FollowCamera camera;
    EXPECT_NE(camera.FindComponent<NS::Obj::ThirdPersonFollow>(), nullptr);
    EXPECT_NE(camera.FindComponent<NS::Game::Level::FollowCameraFeed>(), nullptr);
}

TEST(ActorClass, LightHasDirectionalLight)
{
    const NS::Obj::Light light;
    EXPECT_NE(light.FindComponent<NS::Obj::DirectionalLight>(), nullptr);
}

TEST(ActorClass, PlayerBuildsWholeCompositionInConstructor)
{
    // 以前はシーンのデータが足していた体当たりと当たりの部品も、クラスが組み立てる
    const Player player;
    EXPECT_NE(player.FindComponent<NS::Game::Player::PlayerComponent>(), nullptr);
    EXPECT_NE(player.FindComponent<NS::Obj::PlayerInput>(), nullptr);
    EXPECT_NE(player.FindComponent<NS::Obj::CapsuleCollider>(), nullptr);
    EXPECT_NE(player.FindComponent<NS::Game::Level::CollisionInput>(), nullptr);
    EXPECT_NE(player.FindComponent<NS::Game::Level::ImpactResolver>(), nullptr);
    EXPECT_NE(player.FindComponent<NS::Obj::HitReaction>(), nullptr);
    const NS::Obj::HitSensor* body = player.FindComponent<NS::Obj::HitSensor>();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Type(), NS::Obj::HitSensorType::PlayerBody);
}
