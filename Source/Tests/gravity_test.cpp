#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

TEST(Gravity, DefaultsToDownWithoutScene)
{
    NS::SceneActor actor;
    EXPECT_EQ(actor.GravityDirection(), (NS::Vector3{0.0f, -1.0f, 0.0f}));
    NS::Vector3 velocity{1.0f, 2.0f, 3.0f};
    actor.AddGravity(velocity, 25.0f, 0.5f);
    EXPECT_EQ(velocity, (NS::Vector3{1.0f, -10.5f, 3.0f}));
}

// 重力は真下に決まっている。古いシーンに向きの欄が残っていても読まず、保存でも書かない
TEST(Gravity, SceneFileCannotTurnGravity)
{
    nlohmann::json doc = NS::MakeSceneJson();
    doc["environment"]["gravityDirection"] = nlohmann::json::array({1.0f, 0.0f, 0.0f});
    NS::Scene scene;
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.SpawnActor<NS::SceneActor>("SceneActor");
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GravityDirection(), (NS::Vector3{0.0f, -1.0f, 0.0f}));
    NS::Vector3 velocity{1.0f, 2.0f, 3.0f};
    actor->AddGravity(velocity, 35.0f, 0.5f);
    EXPECT_EQ(velocity, (NS::Vector3{1.0f, -15.5f, 3.0f}));
    EXPECT_EQ(scene.Physics().Gravity(), (NS::Vector3{0.0f, -25.0f, 0.0f}));
    EXPECT_FALSE(scene.ToJson()["environment"].contains("gravityDirection"));
}
