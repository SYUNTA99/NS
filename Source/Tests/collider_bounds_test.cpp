#include <Game/Level/ColliderBounds.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/SphereColliderComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Game::Level::TryGetColliderBounds;
    using NS::Object::BoxColliderComponent;
    using NS::Object::GameObject;
    using NS::Object::SphereColliderComponent;
} // namespace

TEST(ColliderBoundsTest, BoxColliderYieldsItsWorldAABB)
{
    GameObject obj;
    obj.AddComponent<BoxColliderComponent>(NS::Core::Vector3{1.0f, 2.0f, 3.0f});
    obj.Root().SetPosition({10.0f, 0.0f, -4.0f});

    NS::Core::AABB bounds{};
    ASSERT_TRUE(TryGetColliderBounds(obj, bounds));
    EXPECT_FLOAT_EQ(bounds.Center.x, 10.0f);
    EXPECT_FLOAT_EQ(bounds.Center.z, -4.0f);
    EXPECT_FLOAT_EQ(bounds.Extents.x, 1.0f);
    EXPECT_FLOAT_EQ(bounds.Extents.y, 2.0f);
}

TEST(ColliderBoundsTest, SphereColliderYieldsItsWorldAABB)
{
    GameObject obj;
    obj.AddComponent<SphereColliderComponent>(2.0f);
    obj.Root().SetPosition({0.0f, 5.0f, 0.0f});

    NS::Core::AABB bounds{};
    ASSERT_TRUE(TryGetColliderBounds(obj, bounds));
    EXPECT_FLOAT_EQ(bounds.Center.y, 5.0f);
    EXPECT_FLOAT_EQ(bounds.Extents.x, 2.0f);
    EXPECT_FLOAT_EQ(bounds.Extents.y, 2.0f);
    EXPECT_FLOAT_EQ(bounds.Extents.z, 2.0f);
}

TEST(ColliderBoundsTest, SphereColliderScalesWithOwner)
{
    GameObject obj;
    obj.AddComponent<SphereColliderComponent>(0.5f);
    obj.Root().SetScale({2.0f, 2.0f, 2.0f});

    NS::Core::AABB bounds{};
    ASSERT_TRUE(TryGetColliderBounds(obj, bounds));
    EXPECT_FLOAT_EQ(bounds.Extents.x, 1.0f);
}

TEST(ColliderBoundsTest, NoColliderYieldsFalse)
{
    GameObject obj;

    NS::Core::AABB bounds{};
    EXPECT_FALSE(TryGetColliderBounds(obj, bounds));
}
