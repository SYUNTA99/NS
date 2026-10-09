#include "Game/Level/DeathZone.h"
#include "Game/Level/FollowCamera.h"
#include "Game/Level/Goal.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/MapObj.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Player.h"
#include "NSlib/Object/Actors/Light.h"
#include "NSlib/Object/Actors/MapParts.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/BoxCollision.h"
#include "NSlib/Object/SubObjects/DirectionalLight.h"
#include "NSlib/Object/SubObjects/HitReaction.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/MeshCollision.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/PlayerInput.h"
#include "NSlib/Object/SubObjects/Shadow.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"
#include "NSlib/Object/SubObjects/ThirdPersonFollow.h"

#include <gtest/gtest.h>

// 各 Actor のクラスが、自分の部品を Init で組み立てることを縛る

TEST(ActorClass, MapPartsCollidesWithItsMesh)
{
    NS::Obj::MapParts parts;
    parts.EnsureInit();
    const NS::Obj::Model* mesh = parts.ModelSubObj();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "cube");
    // 当たりは見た目のメッシュの三角形。箱や球の当たりは持たない
    EXPECT_NE(NS::Obj::Cast<NS::Obj::MeshCollision>(parts.FindSubObj("Collision")), nullptr);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::BoxCollision>(parts.FindSubObj("Collision")), nullptr);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::SphereCollision>(parts.FindSubObj("Collision")), nullptr);
}

TEST(ActorClass, MapObjOwnsMotionAndStaticCollision)
{
    NS::Game::Level::MapObj obj;
    obj.EnsureInit();
    EXPECT_NE(obj.ModelSubObj(), nullptr);
    EXPECT_NE(NS::Obj::Cast<NS::Obj::SphereCollision>(obj.FindSubObj("Collision")), nullptr);
    EXPECT_EQ(obj.FindSubObj("Breakable"), nullptr);
    EXPECT_EQ(obj.FindSubObj("LaunchedBody"), nullptr);
    // 影は種類の既定値が足す部品。Init は積まない
    EXPECT_EQ(obj.ShadowSubObj(), nullptr);
    // 体当たりは物の体のセンサーで調べられ、受け方と尾は自分で持つ
    const NS::Obj::HitSensor* sensor = obj.BodySensorSubObj();
    ASSERT_NE(sensor, nullptr);
    EXPECT_TRUE(NS::Game::Level::IsSensorKind(*sensor, NS::Game::Level::SensorKind::MapObjBody));
    EXPECT_EQ(obj.FindSubObj("TackleReaction"), nullptr);
    EXPECT_NE(NS::Obj::Cast<NS::Game::Level::LaunchEffects>(obj.FindSubObj("LaunchEffects")), nullptr);
    EXPECT_NE(obj.GetStateMachine(), nullptr);
    EXPECT_FALSE(obj.IsFlying());
}

TEST(ActorClass, GoalHasMarkerAndArea)
{
    NS::Game::Level::Goal goal;
    goal.EnsureInit();
    EXPECT_NE(goal.ModelSubObj(), nullptr);
    const NS::Obj::HitSensor* area = goal.BodySensorSubObj();
    ASSERT_NE(area, nullptr);
    EXPECT_TRUE(NS::Game::Level::IsSensorKind(*area, NS::Game::Level::SensorKind::Area));
}

TEST(ActorClass, DeathZoneIsBoxAreaWithoutTerrainCollision)
{
    NS::Game::Level::DeathZone zone;
    zone.EnsureInit();
    const NS::Obj::ShapeHitSensor* area = NS::Obj::Cast<NS::Obj::ShapeHitSensor>(zone.BodySensorSubObj());
    ASSERT_NE(area, nullptr);
    EXPECT_TRUE(NS::Game::Level::IsSensorKind(*area, NS::Game::Level::SensorKind::Area));
    EXPECT_EQ(area->Shape(), NS::Obj::HitSensorShape::Box);
    EXPECT_GE(area->BoxHalfExtents().x, 100.0f);
    // 地形の当たりを持つと、落ちてきたプレイヤーが上面に着地してしまう
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::BoxCollision>(zone.FindSubObj("Collision")), nullptr);
}

TEST(ActorClass, FollowCameraHasFollowAndFeed)
{
    NS::Game::Level::FollowCamera camera;
    camera.EnsureInit();
    EXPECT_NE(NS::Obj::Cast<NS::Obj::ThirdPersonFollow>(camera.FindSubObj("Vcam")), nullptr);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::ThirdPersonFollow>(camera.FindSubObj("Vcam")), &camera.Vcam());
}

TEST(ActorClass, LightHasDirectionalLight)
{
    NS::Obj::Light light;
    light.EnsureInit();
    EXPECT_NE(NS::Obj::Cast<NS::Obj::DirectionalLight>(light.FindSubObj("DirectionalLight")), nullptr);
}

TEST(ActorClass, PlayerBuildsWholeCompositionInInit)
{
    // 以前はシーンのデータが足していた体当たりと当たりの部品も、クラスが組み立てる
    Player player;
    player.EnsureInit();
    EXPECT_NE(NS::Obj::Cast<NS::Obj::Body>(player.FindSubObj("Movement")), nullptr);
    EXPECT_NE(NS::Obj::Cast<NS::Obj::PlayerInput>(player.FindSubObj("Input")), nullptr);
    EXPECT_EQ(player.FindSubObj("PlayerInputRelay"), nullptr);
    EXPECT_NE(player.GetStateMachine(), nullptr);
    EXPECT_EQ(player.Phase(), NS::Obj::UpdatePhase::Player);
    EXPECT_EQ(&player.Input(), NS::Obj::Cast<NS::Obj::PlayerInput>(player.FindSubObj("Input")));
    EXPECT_EQ(&player.Body(), NS::Obj::Cast<NS::Obj::Body>(player.FindSubObj("Movement")));
    EXPECT_NE(NS::Obj::Cast<NS::Game::Level::ImpactResolver>(player.FindSubObj("ImpactResolver")), nullptr);
    EXPECT_NE(player.HitReactionSubObj(), nullptr);
    const NS::Obj::HitSensor* body = player.BodySensorSubObj();
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(NS::Game::Level::IsSensorKind(*body, NS::Game::Level::SensorKind::PlayerBody));
}

TEST(SensorKinds, SetKindIsSeenByIsKindOnlyForThatKind)
{
    // 付けた種類にだけ真で、他の種類と未設定には偽
    NS::Obj::ShapeHitSensor sensor;
    const NS::Game::Level::SensorKind kinds[] = {NS::Game::Level::SensorKind::PlayerBody,
                                                 NS::Game::Level::SensorKind::MapObjBody,
                                                 NS::Game::Level::SensorKind::Area};
    for (const NS::Game::Level::SensorKind set : kinds)
    {
        NS::Game::Level::SetSensorKind(sensor, set);
        for (const NS::Game::Level::SensorKind asked : kinds)
        {
            EXPECT_EQ(NS::Game::Level::IsSensorKind(sensor, asked), set == asked);
        }
        EXPECT_FALSE(NS::Game::Level::IsSensorKind(sensor, NS::Game::Level::SensorKind::Unset));
    }
}

TEST(SensorKinds, NewSensorIsUnsetAndUnsetCanBeWrittenBack)
{
    // 作ったばかりのセンサーはどの種類でもない。付け忘れたセンサーが黙って体当たりの相手にならない
    NS::Obj::ShapeHitSensor sensor;
    EXPECT_EQ(sensor.Kind(), 0u);
    EXPECT_FALSE(NS::Game::Level::IsSensorKind(sensor, NS::Game::Level::SensorKind::MapObjBody));
    // 未設定を書くと未設定へ戻る
    NS::Game::Level::SetSensorKind(sensor, NS::Game::Level::SensorKind::Area);
    NS::Game::Level::SetSensorKind(sensor, NS::Game::Level::SensorKind::Unset);
    EXPECT_FALSE(NS::Game::Level::IsSensorKind(sensor, NS::Game::Level::SensorKind::Area));
    EXPECT_EQ(sensor.Kind(), 0u);
}
