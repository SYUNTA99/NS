#include <gtest/gtest.h>
#include <Runtime/Object/Components/ShadowComponent.h>

namespace
{
    using NS::Object::ShadowComponent;
} // namespace

// 真下の地面探索は PhysicsWorld::RaycastDown が持つ。 検証は physics_world_test.cpp

TEST(ShadowComponentTest, ComputeFadeEndpointsAndClamp)
{
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(0.0f, 12.0f), 1.0f);
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(12.0f, 12.0f), 0.0f);
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(6.0f, 12.0f), 0.5f);
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(20.0f, 12.0f), 0.0f); // maxDist 超過は 0 にクランプ
}

TEST(ShadowComponentTest, ComputeFadeReturnsZeroForNonPositiveMaxDist)
{
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(1.0f, -5.0f), 0.0f);
}
