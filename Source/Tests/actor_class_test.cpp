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
#include "NSlib/Object/Components/Body.h"
#include "NSlib/Object/Components/BoxCollision.h"
#include "NSlib/Object/Components/DirectionalLight.h"
#include "NSlib/Object/Components/HitReaction.h"
#include "NSlib/Object/Components/HitSensor.h"
#include "NSlib/Object/Components/MeshCollision.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Components/PlayerInput.h"
#include "NSlib/Object/Components/Shadow.h"
#include "NSlib/Object/Components/SphereCollision.h"
#include "NSlib/Object/Components/ThirdPersonFollow.h"

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
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::MeshCollision>(parts.Part("Collision")), nullptr);
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::BoxCollision>(parts.Part("Collision")), nullptr);
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::SphereCollision>(parts.Part("Collision")), nullptr);
}

TEST(ActorClass, MapObjOwnsMotionAndStaticCollision)
{
    const NS::Game::Level::MapObj obj;
    EXPECT_NE(obj.ModelPart(), nullptr);
    EXPECT_NE(NS::Obj::ComponentCast<NS::Obj::SphereCollision>(obj.Part("Collision")), nullptr);
    EXPECT_EQ(obj.Part("Breakable"), nullptr);
    EXPECT_EQ(obj.Part("LaunchedBody"), nullptr);
    // 影は種類の既定値が足す部品。コンストラクタは積まない
    EXPECT_EQ(obj.ShadowPart(), nullptr);
    // 体当たりは物の体のセンサーで調べられ、受け方と尾は自分で持つ
    const NS::Obj::HitSensor* sensor = obj.BodySensorPart();
    ASSERT_NE(sensor, nullptr);
    EXPECT_TRUE(NS::Game::Level::IsSensorKind(*sensor, NS::Game::Level::SensorKind::MapObjBody));
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
    EXPECT_TRUE(NS::Game::Level::IsSensorKind(*area, NS::Game::Level::SensorKind::Area));
}

TEST(ActorClass, DeathZoneIsBoxAreaWithoutTerrainCollision)
{
    const NS::Game::Level::DeathZone zone;
    const NS::Obj::ShapeHitSensor* area = NS::Obj::ComponentCast<NS::Obj::ShapeHitSensor>(zone.BodySensorPart());
    ASSERT_NE(area, nullptr);
    EXPECT_TRUE(NS::Game::Level::IsSensorKind(*area, NS::Game::Level::SensorKind::Area));
    EXPECT_EQ(area->Shape(), NS::Obj::HitSensorShape::Box);
    EXPECT_GE(area->BoxHalfExtents().x, 100.0f);
    // 地形の当たりを持つと、落ちてきたプレイヤーが上面に着地してしまう
    EXPECT_EQ(NS::Obj::ComponentCast<NS::Obj::BoxCollision>(zone.Part("Collision")), nullptr);
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
    EXPECT_NE(NS::Obj::ComponentCast<NS::Game::Level::ImpactResolver>(player.Part("ImpactResolver")), nullptr);
    EXPECT_NE(player.HitReactionPart(), nullptr);
    const NS::Obj::HitSensor* body = player.BodySensorPart();
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
