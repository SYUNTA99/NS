#include "Game/Level/FollowCamera.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "NSlib/Object/Actors/Light.h"
#include "NSlib/Object/Scene/Scene.h"
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
    // 持ち主が消えても残るので、空のシーンとは比べない
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

TEST(SubObjectLifecycle, PlayerIndicatorsRegisterOnlyWhileAppeared)
{
    NS::Obj::Scene scene;
    Player* player = scene.SpawnTransient<Player>();
    // 重ね描きはロックオンの枠と突進の矢印
    EXPECT_EQ(RegisteredWhileAppeared(scene, *player), (Registrations{.renderables = 3, .overlays = 2, .sensors = 1}));
}

TEST(SubObjectLifecycle, LightRegistersOnlyWhileAppeared)
{
    NS::Obj::Scene scene;
    NS::Obj::Light* light = scene.SpawnTransient<NS::Obj::Light>();
    EXPECT_EQ(RegisteredWhileAppeared(scene, *light), (Registrations{.lights = 1}));
}

TEST(SubObjectLifecycle, FollowCameraRegistersOnlyWhileAppeared)
{
    NS::Obj::Scene scene;
    GL::Level::FollowCamera* camera = scene.SpawnTransient<GL::Level::FollowCamera>();
    EXPECT_EQ(RegisteredWhileAppeared(scene, *camera), (Registrations{.cameras = 1}));
}
