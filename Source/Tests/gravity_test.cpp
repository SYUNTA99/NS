#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Gravity.h"
#include "NSlib/Object/Scene/Scene.h"

#include <gtest/gtest.h>

TEST(Gravity, DefaultsToDownWithoutScene)
{
    NS::Obj::Actor actor;
    EXPECT_EQ(NS::Obj::GravityDirection(actor), (NS::Vector3{0.0f, -1.0f, 0.0f}));
    NS::Vector3 velocity{1.0f, 2.0f, 3.0f};
    NS::Obj::AddGravity(actor, velocity, 25.0f, 0.5f);
    EXPECT_EQ(velocity, (NS::Vector3{1.0f, -10.5f, 3.0f}));
}

TEST(Gravity, UsesSceneDirectionAndPassedStrength)
{
    NS::Obj::Scene scene;
    scene.SetGravityDirection(NS::Vector3{3.0f, 0.0f, 0.0f});
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(NS::Obj::GravityDirection(*actor), (NS::Vector3{1.0f, 0.0f, 0.0f}));
    NS::Vector3 velocity{1.0f, 2.0f, 3.0f};
    NS::Obj::AddGravity(*actor, velocity, 35.0f, 0.5f);
    EXPECT_EQ(velocity, (NS::Vector3{18.5f, 2.0f, 3.0f}));
}
