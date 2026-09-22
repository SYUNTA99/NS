#include <Runtime/Object/Scene/Scene.h>

#include <Jolt/Core/Factory.h>
#include <gtest/gtest.h>

TEST(JoltInitialization, RegisterTypesRunsThroughSceneConstruction)
{
    NS::Obj::Scene scene;

    EXPECT_EQ(scene.Physics().BodyCount(), 0u);
    ASSERT_NE(JPH::Factory::sInstance, nullptr);
    EXPECT_NE(JPH::Factory::sInstance->Find("BoxShapeSettings"), nullptr);
}
