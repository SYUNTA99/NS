#include "NSlib/CoreObject/Scene/Scene.h"
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

TEST(Gravity, UsesSceneDirectionAndPassedStrength)
{
    NS::Scene scene;
    scene.SetGravityDirection(NS::Vector3{3.0f, 0.0f, 0.0f});
    NS::SceneActor* actor = scene.SpawnTransient<NS::SceneActor>();
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GravityDirection(), (NS::Vector3{1.0f, 0.0f, 0.0f}));
    NS::Vector3 velocity{1.0f, 2.0f, 3.0f};
    actor->AddGravity(velocity, 35.0f, 0.5f);
    EXPECT_EQ(velocity, (NS::Vector3{18.5f, 2.0f, 3.0f}));
}
