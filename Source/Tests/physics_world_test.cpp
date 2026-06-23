#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/PhysicsWorld.h>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Vector3;
    using NS::Physics::PhysicsWorld;
    using NS::Physics::Sphere;

    AABB MakeBox(const Vector3& center, const Vector3& extents)
    {
        AABB b;
        b.Center = center;
        b.Extents = extents;
        return b;
    }
} // namespace

// 既定構築の world は空
TEST(PhysicsWorldTest, DefaultIsEmpty)
{
    PhysicsWorld world;
    EXPECT_TRUE(world.IsEmpty());
    EXPECT_TRUE(world.Aabbs().empty());
}

// AABB を足すと非空になり Aabbs に反映される
TEST(PhysicsWorldTest, AddAabbReflectsInAccessors)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({1.0f, 2.0f, 3.0f}, {0.5f, 0.5f, 0.5f}));

    EXPECT_FALSE(world.IsEmpty());
    ASSERT_EQ(world.Aabbs().size(), 1u);
    EXPECT_FLOAT_EQ(world.Aabbs()[0].Center.x, 1.0f);
}

// AABB 以外の channel を足しても非空判定になる (sphere は AABB channel に入らない)
TEST(PhysicsWorldTest, NonAabbChannelsCountTowardNonEmpty)
{
    PhysicsWorld world;
    Sphere s;
    s.center = Vector3{0.0f, 0.0f, 0.0f};
    s.radius = 1.0f;
    world.AddSphere(s);

    EXPECT_FALSE(world.IsEmpty());
    EXPECT_TRUE(world.Aabbs().empty());
}

// Clear で全 channel と grid が空に戻る
TEST(PhysicsWorldTest, ClearResetsAllChannels)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    world.BuildBroadphase();

    world.Clear();

    EXPECT_TRUE(world.IsEmpty());
    EXPECT_TRUE(world.Aabbs().empty());
}

// 空 world で BuildBroadphase を呼んでも安全
TEST(PhysicsWorldTest, BuildBroadphaseOnEmptyIsSafe)
{
    PhysicsWorld world;
    world.BuildBroadphase();
    EXPECT_TRUE(world.IsEmpty());
}
