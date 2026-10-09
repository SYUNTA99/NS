#include "Game/Level/CourseDirector.h"
#include "Game/Level/Goal.h"
#include "Game/Player.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/ScreenFade.h"

#include <gtest/gtest.h>

TEST(GameTuning, GoalFadeUsesTheSavedGoalFields)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json playerDoc = NS::Obj::MakePrototypeJson<Player>();
    NS::Obj::SetObjectJsonId(playerDoc, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(playerDoc));
    nlohmann::json goalDoc = NS::Obj::MakePrototypeJson<GL::Level::Goal>();
    NS::Obj::SetObjectJsonId(goalDoc, 2);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(goalDoc));
    scene.LoadJson(std::move(doc));
    NS::Obj::Actor* goal = scene.Objects().FindByObjectId(2);
    ASSERT_NE(goal, nullptr);
    NS::Obj::SubObject* params = goal->FindSubObj("Params");
    ASSERT_NE(params, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*params, {{"クリアの暗転秒", 0.1f}, {"クリアの明転秒", 0.2f}}), 0u);
    const nlohmann::json saved = scene.ToJson();
    scene.LoadJson(saved);
    (void)scene.BeginPlayBaseline();
    GL::Level::CourseDirector* director = NS::Obj::FindSceneObj<GL::Level::CourseDirector>(scene);
    ASSERT_NE(director, nullptr);
    scene.HitSensors().OnTick();
    director->OnTick();
    ASSERT_TRUE(director->IsClearing());
    NS::Obj::ScreenFade& fade = const_cast<NS::Obj::ScreenFade&>(director->Fade());
    fade.Advance(0.05f);
    EXPECT_NEAR(fade.Alpha(), 0.5f, 0.001f);
    fade.Advance(0.05f);
    ASSERT_TRUE(fade.IsBlack());
    director->OnTick();
    fade.Advance(0.1f);
    EXPECT_NEAR(fade.Alpha(), 0.5f, 0.001f);
}
