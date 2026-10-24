#include "Editor/EditorActors.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/GoalActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/Render/ScreenFade.h"

#include <gtest/gtest.h>

TEST(GameTuning, GoalFadeUsesTheEditedGoalFields)
{
    NS::Scene scene;
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
    ASSERT_EQ(NS::ApplyJsonFields(&goal->Params(), {{"クリアの暗転秒", 0.1f}, {"クリアの明転秒", 0.2f}}), 0u);
    GL::Level::CourseDirector* director = NS::FindSceneSystem<GL::Level::CourseDirector>(&scene);
    ASSERT_NE(director, nullptr);
    scene.HitSensors().OnTick();
    director->OnTick();
    ASSERT_TRUE(director->IsClearing());
    NS::Gfx::ScreenFade& fade = scene.Fade();
    fade.Advance(0.05f);
    EXPECT_NEAR(fade.Alpha(), 0.5f, 0.001f);
    fade.Advance(0.05f);
    ASSERT_TRUE(fade.IsBlack());
    director->OnTick();
    fade.Advance(0.1f);
    EXPECT_NEAR(fade.Alpha(), 0.5f, 0.001f);
}
