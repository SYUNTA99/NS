#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Phys/PhysicsScene.h"

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

    Registrations Count(NS::Scene& scene)
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
    Registrations RegisteredWhileAppeared(NS::Scene& scene, NS::SceneActor& actor)
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

TEST(ClassObjLifecycle, BaseFramePartsRegisterOnlyWhileAppeared)
{
    NS::Scene scene;
    GL::Level::PropActor* actor = scene.SpawnTransient<GL::Level::PropActor>();
    // 模型は本体と残像の 2 つ、影は 1 つを描く物に入れる
    EXPECT_EQ(RegisteredWhileAppeared(scene, *actor), (Registrations{.renderables = 3, .sensors = 1, .bodies = 1}));
}

// 派生の部品 (重ね描き・光・仮想カメラ) は持ち主が始まりに 1 回入れ、出し入れでは外さない。
// 消えている間は、仕組みが部品の IsActive を見て使わない

TEST(ClassObjLifecycle, PlayerIndicatorsStayRegisteredAndHideWhileDisappeared)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnTransient<PlayerActor>();
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

TEST(ClassObjLifecycle, LightStaysRegisteredAndIsSkippedWhileDisappeared)
{
    NS::Scene scene;
    NS::LightActor* light = scene.SpawnTransient<NS::LightActor>();
    const NS::DirectionalLight* sun = &light->Light();
    EXPECT_EQ(RegisteredWhileAppeared(scene, *light), Registrations{});
    EXPECT_EQ(scene.LightCount(), 1u);
    // 描く側 (SceneRenderer::ResolveSceneSettings) は持ち主が出ていない光を飛ばす
    EXPECT_FALSE(sun->IsOwnerAppeared());
    light->Appear();
    EXPECT_TRUE(sun->IsOwnerAppeared());
}

TEST(ClassObjLifecycle, FollowCameraStaysRegisteredAndIsSkippedWhileDisappeared)
{
    NS::Scene scene;
    GL::Level::FollowCameraActor* camera = scene.SpawnTransient<GL::Level::FollowCameraActor>();
    const NS::CameraManager* cameras = scene.GetCameraManager();
    EXPECT_EQ(RegisteredWhileAppeared(scene, *camera), Registrations{});
    EXPECT_EQ(cameras->VirtualCameraCount(), 1u);
    EXPECT_FALSE(cameras->ViewPose().has_value());
    camera->Appear();
    EXPECT_TRUE(cameras->ViewPose().has_value());
}
