#include "Editor/EditorActors.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/GoalActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Gfx/Render/ScreenFade.h"

#include <gtest/gtest.h>

TEST(GameTuning, GoalFadeUsesTheEditedGoalFields)
{
    GL::Level::GameScene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json playerDoc = NS::Editor::MakePrototypeJson<PlayerActor>();
    NS::SetActorJsonId(playerDoc, 1);
    NS::SceneJsonActors(doc).push_back(std::move(playerDoc));
    nlohmann::json goalDoc = NS::Editor::MakePrototypeJson<GL::Level::GoalActor>();
    NS::SetActorJsonId(goalDoc, 2);
    NS::SceneJsonActors(doc).push_back(std::move(goalDoc));
    scene.ResetFromJson(std::move(doc));
    GL::Level::GoalActor* goal = NS::Cast<GL::Level::GoalActor>(scene.Actors().FindByActorId(2));
    ASSERT_NE(goal, nullptr);
    // 暗転は 2 歩、明転は 4 歩。歩ごとに暗転が 1 歩分進む
    const float step = NS::OS::FrameTimer::FixedDelta();
    ASSERT_EQ(NS::ApplyJsonFields(&goal->Params(), {{"クリアの暗転秒", step * 2.0f}, {"クリアの明転秒", step * 4.0f}}), 0u);
    const NS::Gfx::ScreenFade& fade = scene.Fade();

    scene.OnUpdate();
    ASSERT_TRUE(scene.IsClearing());
    EXPECT_NEAR(fade.Alpha(), 0.5f, 0.001f);
    scene.OnUpdate();
    ASSERT_TRUE(fade.IsBlack());
    scene.OnUpdate();
    EXPECT_NEAR(fade.Alpha(), 0.75f, 0.001f);
}
