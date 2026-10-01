#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Gravity.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

TEST(Gravity, DefaultsToDownWithoutScene)
{
    NS::Obj::Actor actor;
    EXPECT_EQ(NS::Obj::GravityDirection(actor), (NS::Core::Vector3{0.0f, -1.0f, 0.0f}));
    NS::Core::Vector3 velocity{1.0f, 2.0f, 3.0f};
    NS::Obj::AddGravity(actor, velocity, 25.0f, 0.5f);
    EXPECT_EQ(velocity, (NS::Core::Vector3{1.0f, -10.5f, 3.0f}));
}

TEST(Gravity, UsesSceneDirectionAndPassedStrength)
{
    NS::Obj::Scene scene;
    scene.SetGravityDirection(NS::Core::Vector3{3.0f, 0.0f, 0.0f});
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(NS::Obj::GravityDirection(*actor), (NS::Core::Vector3{1.0f, 0.0f, 0.0f}));
    NS::Core::Vector3 velocity{1.0f, 2.0f, 3.0f};
    NS::Obj::AddGravity(*actor, velocity, 35.0f, 0.5f);
    EXPECT_EQ(velocity, (NS::Core::Vector3{18.5f, 2.0f, 3.0f}));
}
