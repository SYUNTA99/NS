#include "Editor/EditorActors.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/ScreenFlashDirector.h"
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

// 頼んだフレームは始めの濃さのまま。次の更新から、残りのフレーム数に比例して薄れる
TEST(ScreenFlashDirector, StartsAtFullAlphaAndFadesFromTheNextTick)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::ScreenFlashDirector& flash = scene.ScreenFlash();

    flash.Start(player, 3, 0.9f);
    EXPECT_EQ(flash.FramesRemaining(), 3);
    EXPECT_FLOAT_EQ(flash.Alpha(), 0.9f);
    flash.OnTick();
    EXPECT_EQ(flash.FramesRemaining(), 3);
    EXPECT_FLOAT_EQ(flash.Alpha(), 0.9f);
    flash.OnTick();
    EXPECT_EQ(flash.FramesRemaining(), 2);
    EXPECT_NEAR(flash.Alpha(), 0.6f, 0.0001f);
    flash.OnTick();
    flash.OnTick();
    EXPECT_EQ(flash.FramesRemaining(), 0);
    EXPECT_FLOAT_EQ(flash.Alpha(), 0.0f);
}

// 始める・薄める・濃さを変える・止めるたびに、今の濃さを描く仕組みへ書く。描く仕組みは描く時にこの値だけを読む
TEST(ScreenFlashDirector, WritesTheAlphaToTheSceneRenderer)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::ScreenFlashDirector& flash = scene.ScreenFlash();
    const NS::SceneRenderer& renderer = *scene.GetSceneRenderer();
    EXPECT_FLOAT_EQ(renderer.ScreenFlashAlpha(), 0.0f);

    flash.Start(player, 3, 0.9f);
    EXPECT_FLOAT_EQ(renderer.ScreenFlashAlpha(), 0.9f);
    flash.OnTick();
    flash.OnTick();
    EXPECT_NEAR(renderer.ScreenFlashAlpha(), 0.6f, 0.0001f);
    flash.Scale(player, 0.5f);
    EXPECT_NEAR(renderer.ScreenFlashAlpha(), 0.3f, 0.0001f);
    flash.Stop(player);
    EXPECT_FLOAT_EQ(renderer.ScreenFlashAlpha(), 0.0f);
}

// 止めると、止めた物が頼んだ白だけが消える。ほかの物が頼んだ物は残る
TEST(ScreenFlashDirector, StopClearsOnlyWhatTheRequesterStarted)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::SceneActor* goal = scene.Actors().FindByActorId(2);
    ASSERT_NE(goal, nullptr);
    NS::ScreenFlashDirector& flash = scene.ScreenFlash();

    flash.Start(goal, 3, 1.0f);
    flash.Stop(player);
    EXPECT_EQ(flash.FramesRemaining(), 3);
    flash.Stop(goal);
    EXPECT_EQ(flash.FramesRemaining(), 0);
}

// 持ち主の HitReaction を止めると、持ち主が頼んだ白が消える
TEST(ScreenFlashDirector, HitReactionStopClearsTheOwnersFlash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    NS::ScreenFlashDirector& flash = scene.ScreenFlash();
    flash.Start(player, 4, 1.0f);
    EXPECT_EQ(flash.FramesRemaining(), 4);
    reaction->Stop(NS::HitReactionSelf{.actor = player, .model = player->ModelClassObj()});
    EXPECT_EQ(flash.FramesRemaining(), 0);
}

// シーンがアクターを組み直すと、前の世界で頼んだ白を次の世界へ残さない。頼み手は、消える時に自分で止めないゴールにする
TEST(ScreenFlashDirector, RebuildingTheSceneClearsTheFlash)
{
    NS::Scene scene;
    ASSERT_NE(PlaceTwo(scene), nullptr);
    const NS::SceneActor* goal = scene.Actors().FindByActorId(2);
    ASSERT_NE(goal, nullptr);
    scene.ScreenFlash().Start(goal, 30, 1.0f);

    scene.ResetFromJson(NS::MakeSceneJson());
    EXPECT_EQ(scene.ScreenFlash().FramesRemaining(), 0);
}

// シーンが止まっている歩も StepViewStopped で進むので、止めている間も薄れる
TEST(ScreenFlashDirector, KeepsFadingWhileTheSceneIsStopped)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTwo(scene);
    ASSERT_NE(player, nullptr);
    scene.RequestStopScene(player, 10);
    scene.ScreenFlash().Start(player, 3, 1.0f);
    scene.OnUpdate();
    EXPECT_EQ(scene.ScreenFlash().FramesRemaining(), 3);
    scene.OnUpdate();
    EXPECT_EQ(scene.ScreenFlash().FramesRemaining(), 2);
}
