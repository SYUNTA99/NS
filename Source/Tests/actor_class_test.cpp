#include "Game/Level/Breakable.h"
#include "Game/Level/CollisionInput.h"
#include "Game/Level/FollowCamera.h"
#include "Game/Level/FollowCameraFeed.h"
#include "Game/Level/Goal.h"
#include "Game/Level/GoalComponent.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/KillZoneComponent.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/Actors/Light.h"
#include "Runtime/Object/Actors/MapParts.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"

#include <gtest/gtest.h>

// 各 Actor のクラスが、自分の部品をコンストラクタで組み立てることを縛る
// 部品の組み立てはクラスだけが決めるので、ここが種類ごとの構成の唯一の出所になる

TEST(ActorClass, MapPartsHasMeshOnly)
{
    const NS::Obj::MapParts parts;
    const NS::Obj::MeshRenderer* mesh = parts.FindComponent<NS::Obj::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "cube");
    // 当たりの形は置く種類のデータが足す
    EXPECT_EQ(parts.FindComponent<NS::Obj::Collider>(), nullptr);
}

TEST(ActorClass, MapObjIsKinematicLaunchableBody)
{
    const NS::Game::Level::MapObj obj;
    EXPECT_NE(obj.FindComponent<NS::Obj::MeshRenderer>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Obj::SphereCollider>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Game::Level::Breakable>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Game::Level::LaunchedBody>(), nullptr);
    EXPECT_NE(obj.FindComponent<NS::Obj::Shadow>(), nullptr);
    const NS::Obj::RigidBody* body = obj.FindComponent<NS::Obj::RigidBody>();
    ASSERT_NE(body, nullptr);
    // 置かれている間は動かない
    EXPECT_TRUE(body->IsKinematic());
}

TEST(ActorClass, GoalHasMarkerAndTrigger)
{
    const NS::Game::Level::Goal goal;
    EXPECT_NE(goal.FindComponent<NS::Obj::MeshRenderer>(), nullptr);
    EXPECT_NE(goal.FindComponent<NS::Game::Level::GoalComponent>(), nullptr);
}

TEST(ActorClass, KillZoneBoxIsTrigger)
{
    const NS::Game::Level::KillZone zone;
    EXPECT_NE(zone.FindComponent<NS::Game::Level::KillZoneComponent>(), nullptr);
    const NS::Obj::BoxCollider* box = zone.FindComponent<NS::Obj::BoxCollider>();
    ASSERT_NE(box, nullptr);
    // 固形だと落ちてきたプレイヤーが上面に着地してしまう
    EXPECT_TRUE(box->IsTrigger());
    EXPECT_GE(box->HalfExtents().x, 100.0f);
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
}
