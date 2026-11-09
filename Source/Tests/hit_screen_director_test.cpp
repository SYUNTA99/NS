#include "Editor/EditorActors.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/HitScreenDirector.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"
#include "NSlib/Core/Windows/Input.h"
#include "Tests/TestSceneSetup.h"

#include <gtest/gtest.h>

namespace
{
    // ゴールは、自機でない頼み手として使う
    PlayerActor* PlaceTwo(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "PlayerActor", 1, NS::Vector3{0.0f, 1.0f, 0.0f});
        AddActorJson(doc, "GoalActor", 2, NS::Vector3{0.0f, 0.5f, 5.0f});
        scene.ResetFromJson(doc);
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }
} // namespace

// シーンがアクターを組み直すと、前の世界で頼んだ輪・床の波・振動を次の世界へ残さない。
// 頼み手は、消える時に自分で止めないゴールにする
TEST(HitScreenDirector, RebuildingTheSceneClearsEverything)
{
    GL::Level::GameScene scene;
    ASSERT_NE(PlaceTwo(scene), nullptr);
    const NS::SceneActor* goal = scene.Actors().FindByActorId(2);
    ASSERT_NE(goal, nullptr);
    GL::Level::HitScreenDirector& screen = scene.HitScreen();
    GL::Level::HitGroundWaveDesc wave;
    wave.strength.count = 1;
    wave.strength.keys[0] = NS::Curve::Key{0.0f, 1.0f};
    wave.frames = 30;
    screen.StartGroundWave(goal, wave);
    ASSERT_GT(GL::Level::HitScreenDirector::GroundWaveShown(*scene.GetSceneRenderer()).strength, 0.0f);
    GL::Level::HitDistortionRingDesc ring;
    ring.push.count = 1;
    ring.push.keys[0] = NS::Curve::Key{0.0f, 0.03f};
    ring.frames = 30;
    screen.StartDistortionRing(goal, ring);
    ASSERT_GT(scene.GetSceneRenderer()->DistortionRingShown().push, 0.0f);
    NS::PadRumbleDesc pad;
    pad.left.count = 1;
    pad.left.keys[0] = NS::Curve::Key{0.0f, 1.0f};
    pad.frames = 30;
    scene.PadRumble().Start(goal, pad);
    ASSERT_FLOAT_EQ(NS::OS::Input::Get().Gamepad().Vibration().left, 1.0f);

    scene.ResetFromJson(NS::MakeSceneJson());
    EXPECT_FLOAT_EQ(GL::Level::HitScreenDirector::GroundWaveShown(*scene.GetSceneRenderer()).strength, 0.0f);
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.0f);
    EXPECT_FLOAT_EQ(NS::OS::Input::Get().Gamepad().Vibration().left, 0.0f);
}

// シーンが止まっている歩でも、GameScene が StepViewEffects で輪を進める
TEST(HitScreenDirector, DistortionRingKeepsGoingWhileTheSceneIsStopped)
{
    GL::Level::GameScene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    GL::Level::HitDistortionRingDesc desc;
    desc.radius.count = 2;
    desc.radius.keys[0] = NS::Curve::Key{0.0f, 0.0f};
    desc.radius.keys[1] = NS::Curve::Key{10.0f, 1.0f};
    desc.push.count = 1;
    desc.push.keys[0] = NS::Curve::Key{0.0f, 0.03f};
    desc.frames = 10;
    scene.RequestStopScene(player, 10);
    scene.HitScreen().StartDistortionRing(player, desc);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().radius, 0.0f);
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsSceneStopped());
    EXPECT_NEAR(scene.GetSceneRenderer()->DistortionRingShown().radius, 0.1f, 1.0e-6f);
}

TEST(HitScreenDirector, DistortionRingWritesTheSceneForItsFrames)
{
    GL::Level::GameScene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    GL::Level::HitScreenDirector* screen = &scene.HitScreen();
    GL::Level::HitDistortionRingDesc desc;
    desc.center = NS::Vector3{1.0f, 2.0f, 3.0f};
    desc.radius.count = 2;
    desc.radius.keys[0] = NS::Curve::Key{0.0f, 0.0f};
    desc.radius.keys[1] = NS::Curve::Key{4.0f, 0.4f};
    desc.push.count = 1;
    desc.push.keys[0] = NS::Curve::Key{0.0f, 0.03f};
    desc.halfWidth = 0.05f;
    desc.frames = 3;

    screen->StartDistortionRing(player, desc);
    for (int frame = 0; frame < 3; ++frame)
    {
        SCOPED_TRACE(frame);
        const NS::Gfx::DistortionRing& now = scene.GetSceneRenderer()->DistortionRingShown();
        EXPECT_TRUE(now.center == desc.center);
        EXPECT_NEAR(now.radius, 0.1f * static_cast<float>(frame), 1.0e-6f);
        EXPECT_FLOAT_EQ(now.push, 0.03f);
        EXPECT_FLOAT_EQ(now.halfWidth, 0.05f);
        screen->OnTick();
        if (frame == 0)
        {
            // 頼んだフレームの更新では進めない
            EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().radius, 0.0f);
            screen->OnTick();
        }
    }
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.0f);
    // フレーム数 0 以下は出さない
    desc.frames = 0;
    screen->StartDistortionRing(player, desc);
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.0f);
}

// 歪みの輪は、頼んだ物が止めた時だけ消える。持ち主の HitReaction::Stop では消えない
TEST(HitScreenDirector, DistortionRingStopsOnlyForItsRequester)
{
    GL::Level::GameScene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::SceneActor* goal = scene.Actors().FindByActorId(2);
    ASSERT_NE(goal, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    GL::Level::HitScreenDirector* screen = &scene.HitScreen();
    GL::Level::HitDistortionRingDesc desc;
    desc.push.count = 1;
    desc.push.keys[0] = NS::Curve::Key{0.0f, 0.03f};
    desc.frames = 10;

    screen->StartDistortionRing(player, desc);
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.03f);
    screen->StopDistortionRing(goal);
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.03f);
    reaction->Stop(NS::HitReactionSelf{.actor = player, .model = player->ModelClassObj()});
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.03f);
    screen->StopDistortionRing(player);
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.0f);
}

// 弱めると、頼んだ物の輪の押し出しと波の強さだけに倍率が掛かり、始め直さずに続く
TEST(HitScreenDirector, ScaleWeakensOnlyTheRequestersRingAndWave)
{
    GL::Level::GameScene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::SceneActor* goal = scene.Actors().FindByActorId(2);
    ASSERT_NE(goal, nullptr);
    GL::Level::HitScreenDirector& screen = scene.HitScreen();
    GL::Level::HitDistortionRingDesc ring;
    ring.push.count = 1;
    ring.push.keys[0] = NS::Curve::Key{0.0f, 0.1f};
    ring.frames = 10;
    GL::Level::HitGroundWaveDesc wave;
    wave.strength.count = 1;
    wave.strength.keys[0] = NS::Curve::Key{0.0f, 0.8f};
    wave.frames = 10;
    screen.StartDistortionRing(player, ring);
    screen.StartGroundWave(player, wave);

    screen.Scale(goal, 0.5f);
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.1f);
    EXPECT_FLOAT_EQ(GL::Level::HitScreenDirector::GroundWaveShown(*scene.GetSceneRenderer()).strength, 0.8f);
    screen.Scale(player, 0.5f);
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.05f);
    EXPECT_FLOAT_EQ(GL::Level::HitScreenDirector::GroundWaveShown(*scene.GetSceneRenderer()).strength, 0.4f);
    screen.OnTick();
    screen.OnTick();
    EXPECT_FLOAT_EQ(scene.GetSceneRenderer()->DistortionRingShown().push, 0.05f);
    EXPECT_FLOAT_EQ(GL::Level::HitScreenDirector::GroundWaveShown(*scene.GetSceneRenderer()).strength, 0.4f);
}
