#include "Game/Level/FollowCamera.h"
#include "Game/Level/MapObj.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player.h"
#include "NSlib/Object/Actors/Light.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/System/CameraManager.h"
#include "NSlib/Physics/PhysicsScene.h"

#include <gtest/gtest.h>

namespace
{
    // 部品がシーンの仕組みへ入れた数
    struct Registrations
    {
        std::size_t renderables = 0;
        std::size_t overlays = 0;
        std::size_t lights = 0;
        std::size_t cameras = 0;
        std::size_t sensors = 0;
        NS::uint bodies = 0;

        bool operator==(const Registrations&) const = default;
    };

    Registrations Count(NS::Obj::Scene& scene)
    {
        scene.SyncPhysics();
        return Registrations{scene.RenderableCount(),
                             scene.OverlayCount(),
                             scene.LightCount(),
                             scene.GetCameraManager()->VirtualCameraCount(),
                             scene.HitSensors().Sensors().size(),
                             scene.GetPhysicsScene()->BodyCount()};
    }

    // 出ている間と消えている間の差を返す。部品が作るシーンに 1 つの管理役 (当たりの画面の係など) は
    // 持ち主が消えても残るので、空のシーンとは比べない。返る時、actor は消えている
    Registrations RegisteredWhileAppeared(NS::Obj::Scene& scene, NS::Obj::Actor& actor)
    {
        const Registrations appeared = Count(scene);
        actor.Disappear();
        const Registrations disappeared = Count(scene);
        actor.Appear();
        EXPECT_EQ(Count(scene), appeared);
        actor.Disappear();
        EXPECT_EQ(Count(scene), disappeared);
        return Registrations{appeared.renderables - disappeared.renderables,
                             appeared.overlays - disappeared.overlays,
                             appeared.lights - disappeared.lights,
                             appeared.cameras - disappeared.cameras,
                             appeared.sensors - disappeared.sensors,
                             appeared.bodies - disappeared.bodies};
    }
} // namespace

TEST(SubObjectLifecycle, BaseFramePartsRegisterOnlyWhileAppeared)
{
    NS::Obj::Scene scene;
    GL::Level::MapObj* actor = scene.SpawnTransient<GL::Level::MapObj>();
    // 模型は本体と残像の 2 つ、影は 1 つを描く物に入れる
    EXPECT_EQ(RegisteredWhileAppeared(scene, *actor), (Registrations{.renderables = 3, .sensors = 1, .bodies = 1}));
}

// 派生の部品 (重ね描き・光・仮想カメラ) は持ち主が始まりに 1 回入れ、出し入れでは外さない。
// 消えている間は、仕組みが部品の IsActive を見て使わない

TEST(SubObjectLifecycle, PlayerIndicatorsStayRegisteredAndHideWhileDisappeared)
{
    NS::Obj::Scene scene;
    Player* player = scene.SpawnTransient<Player>();
    EXPECT_EQ(RegisteredWhileAppeared(scene, *player), (Registrations{.renderables = 3, .sensors = 1}));
    EXPECT_FALSE(player->TargetIndicator().IsOverlayVisible());
    EXPECT_FALSE(player->SlamIndicator().IsOverlayVisible());

    // 当たりの画面の係も重ね描きに入るので、枠と矢印が入っている事は、外した時に数が減るかで見る
    player->Appear();
    EXPECT_TRUE(player->TargetIndicator().IsOverlayVisible());
    EXPECT_TRUE(player->SlamIndicator().IsOverlayVisible());
    const std::size_t registered = scene.OverlayCount();
    scene.UnregisterOverlay(&player->TargetIndicator());
    scene.UnregisterOverlay(&player->SlamIndicator());
    EXPECT_EQ(scene.OverlayCount(), registered - 2);
}

TEST(SubObjectLifecycle, LightStaysRegisteredAndIsSkippedWhileDisappeared)
{
    NS::Obj::Scene scene;
    NS::Obj::Light* light = scene.SpawnTransient<NS::Obj::Light>();
    const NS::Obj::SubObject* sun = light->FindSubObj("DirectionalLight");
    ASSERT_NE(sun, nullptr);
    EXPECT_EQ(RegisteredWhileAppeared(scene, *light), Registrations{});
    EXPECT_EQ(scene.LightCount(), 1u);
    // 描く側 (SceneRenderer::ResolveSceneSettings) は IsActive が偽の光を飛ばす
    EXPECT_FALSE(sun->IsActive());
    light->Appear();
    EXPECT_TRUE(sun->IsActive());
}

TEST(SubObjectLifecycle, FollowCameraStaysRegisteredAndIsSkippedWhileDisappeared)
{
    NS::Obj::Scene scene;
    GL::Level::FollowCamera* camera = scene.SpawnTransient<GL::Level::FollowCamera>();
    const NS::Obj::CameraManager* cameras = scene.GetCameraManager();
    EXPECT_EQ(RegisteredWhileAppeared(scene, *camera), Registrations{});
    EXPECT_EQ(cameras->VirtualCameraCount(), 1u);
    EXPECT_FALSE(cameras->ViewPose().has_value());
    camera->Appear();
    EXPECT_TRUE(cameras->ViewPose().has_value());
}
