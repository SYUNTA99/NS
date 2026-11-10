#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/ReboundPreview.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/Actors/MapPartsActor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "TestRegistrations.h"

#include <gtest/gtest.h>

namespace
{
    // 部品がシーンの仕組みへ入れた組のうち、有効な数
    struct EnabledRegistrations
    {
        std::size_t renderables = 0;
        std::size_t worldLines = 0;
        std::size_t lights = 0;
        std::size_t cameras = 0;
        std::size_t sensors = 0;
        NS::uint bodies = 0;

        bool operator==(const EnabledRegistrations&) const = default;
    };

    EnabledRegistrations Count(NS::Scene& scene)
    {
        // 世界を止めた 1 歩で当たりを今の姿へ揃えてから数える
        scene.SetSimulationEnabled(false);
        scene.OnUpdate();
        return EnabledRegistrations{scene.GetSceneRenderer()->EnabledModelCount() + scene.GetSceneRenderer()->EnabledShadowCount(),
                             scene.GetSceneRenderer()->EnabledWorldLinesCount(),
                             scene.GetSceneRenderer()->EnabledLightCount(),
                             scene.GetCameraManager()->EnabledVirtualCameraCount(),
                             EnabledSensorCount(scene),
                             scene.GetPhysicsScene()->AttachedBodyCount()};
    }

    // 出ている間と消えている間の差を返す。部品が作るシーンに 1 つの管理役 (当たりの画面の係など) は
    // 持ち主が消えても残るので、空のシーンとは比べない。返る時、actor は消えている
    EnabledRegistrations RegisteredWhileAppeared(NS::Scene& scene, NS::SceneActor* actor)
    {
        NS_ASSERT(Game, actor != nullptr, "RegisteredWhileAppeared: 渡したアクターか部品が無い");
        const EnabledRegistrations appeared = Count(scene);
        actor->Disappear();
        const EnabledRegistrations disappeared = Count(scene);
        actor->Appear();
        EXPECT_EQ(Count(scene), appeared);
        actor->Disappear();
        EXPECT_EQ(Count(scene), disappeared);
        return EnabledRegistrations{appeared.renderables - disappeared.renderables,
                             appeared.worldLines - disappeared.worldLines,
                             appeared.lights - disappeared.lights,
                             appeared.cameras - disappeared.cameras,
                             appeared.sensors - disappeared.sensors,
                             appeared.bodies - disappeared.bodies};
    }
} // namespace

TEST(ClassObjLifecycle, BaseFramePartsRegisterOnlyWhileAppeared)
{
    NS::Scene scene;
    // 地形の部品は模型を描く物に、当たりを物理に入れる。ゴールは模型を描く物に、体のセンサーをセンサーに入れる
    NS::MapPartsActor* parts = scene.SpawnActor<NS::MapPartsActor>("MapPartsActor");
    GL::Level::GoalActor* goal = scene.SpawnActor<GL::Level::GoalActor>("GoalActor");
    EXPECT_EQ(RegisteredWhileAppeared(scene, parts), (EnabledRegistrations{.renderables = 1, .bodies = 1}));
    EXPECT_EQ(RegisteredWhileAppeared(scene, goal), (EnabledRegistrations{.renderables = 1, .sensors = 1}));
}

// 派生が作る部品 (線の部品・光・仮想カメラ) も持ち主が作る時に 1 回入れ、出し入れでは外さない。
// 消えている間は組が無効になり、仕組みが使わない

TEST(ClassObjLifecycle, PlayerIndicatorsStayRegisteredAndHideWhileDisappeared)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnActor<PlayerActor>("PlayerActor");
    // 描く物は体の Model と影の 2 つ。残像は体の Model の写しなので数に入らない
    EXPECT_EQ(RegisteredWhileAppeared(scene, player), (EnabledRegistrations{.renderables = 2, .worldLines = 1, .sensors = 1}));

    // 消えている間も反動の矢印を書く線の部品が入っている事は、外した時に数が減るかで見る
    const std::size_t registered = scene.GetSceneRenderer()->WorldLinesCount();
    NS::EndDraw(&scene, player->WorldLinesClassObj());
    EXPECT_EQ(scene.GetSceneRenderer()->WorldLinesCount(), registered - 1);
}

TEST(ClassObjLifecycle, LightStaysRegisteredAndIsSkippedWhileDisappeared)
{
    NS::Scene scene;
    NS::LightActor* light = scene.SpawnActor<NS::LightActor>("LightActor");
    EXPECT_EQ(RegisteredWhileAppeared(scene, light), EnabledRegistrations{.lights = 1});
    EXPECT_EQ(scene.GetSceneRenderer()->LightCount(), 1u);

    // 消えている間はシーンの照明を書かず、出ると光の露出と向きで書く
    NS::Gfx::RenderSettings defaults{};
    defaults.exposure = 7.0f;
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->ResolveSceneSettings(defaults).exposure, 7.0f);
    light->Appear();
    const NS::Gfx::RenderSettings lit = scene.GetSceneRenderer()->ResolveSceneSettings(defaults);
    EXPECT_FLOAT_EQ(lit.exposure, light->LightClassObj()->Exposure());
    const NS::Vector3 direction = light->LightClassObj()->Direction(light->Root().WorldMatrix());
    EXPECT_NEAR(lit.lightDir.y, direction.y, 1.0e-5f);
}

TEST(ClassObjLifecycle, FollowCameraStaysRegisteredAndIsSkippedWhileDisappeared)
{
    NS::Scene scene;
    GL::Level::FollowCameraActor* camera = scene.SpawnActor<GL::Level::FollowCameraActor>("FollowCameraActor");
    const NS::CameraManager* cameras = scene.GetCameraManager();
    EXPECT_EQ(RegisteredWhileAppeared(scene, camera), EnabledRegistrations{.cameras = 1});
    EXPECT_EQ(cameras->VirtualCameraCount(), 1u);
    EXPECT_FALSE(cameras->ViewPose().has_value());
    camera->Appear();
    EXPECT_TRUE(cameras->ViewPose().has_value());
}
