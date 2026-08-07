#include <gtest/gtest.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/ShadowComponent.h>
#include <vector>

namespace
{
    using NS::Object::ShadowComponent;

    NS::Core::AABB MakeBox(float cx, float cy, float cz, float half = 0.5f)
    {
        return NS::Core::AABB{{cx, cy, cz}, {half, half, half}};
    }
} // namespace

TEST(ShadowComponentTest, GroundBelowFindsNearestAABB)
{
    std::vector<NS::Core::AABB> world;
    world.push_back(MakeBox(0.0f, -2.0f, 0.0f)); // 上面 y=-1.5、origin y=1 から 2.5
    world.push_back(MakeBox(0.0f, -5.0f, 0.0f)); // より遠い
    float outDist = 0.0f;
    const bool hit = ShadowComponent::GroundBelow({0.0f, 1.0f, 0.0f}, world, 12.0f, outDist);
    EXPECT_TRUE(hit);
    EXPECT_NEAR(outDist, 2.5f, 0.001f);
}

TEST(ShadowComponentTest, GroundBelowReturnsFalseWhenNothingDirectlyBelow)
{
    std::vector<NS::Core::AABB> world;
    world.push_back(MakeBox(10.0f, -2.0f, 0.0f)); // 横にずれて真下に無い
    float outDist = -1.0f;
    const bool hit = ShadowComponent::GroundBelow({0.0f, 1.0f, 0.0f}, world, 12.0f, outDist);
    EXPECT_FALSE(hit);
}

TEST(ShadowComponentTest, GroundBelowRespectsMaxDist)
{
    std::vector<NS::Core::AABB> world;
    world.push_back(MakeBox(0.0f, -20.0f, 0.0f)); // 上面 y=-19.5、距離 20.5 > maxDist
    float outDist = -1.0f;
    const bool hit = ShadowComponent::GroundBelow({0.0f, 1.0f, 0.0f}, world, 12.0f, outDist);
    EXPECT_FALSE(hit);
}

TEST(ShadowComponentTest, ComputeFadeEndpointsAndClamp)
{
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(0.0f, 12.0f), 1.0f);
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(12.0f, 12.0f), 0.0f);
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(6.0f, 12.0f), 0.5f);
    EXPECT_FLOAT_EQ(ShadowComponent::ComputeFade(20.0f, 12.0f), 0.0f); // maxDist 超過は 0 にクランプ
}
