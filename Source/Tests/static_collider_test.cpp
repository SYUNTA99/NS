#include <gtest/gtest.h>

#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/StaticColliderComponent.h>
#include <Framework/Scene/Transform.h>

namespace
{
    using NS::Scene::GameObject;
    using NS::Scene::StaticColliderComponent;
} // namespace

TEST(StaticColliderTest, DefaultHalfExtentsAreHalfMeterCube)
{
    StaticColliderComponent sc(nullptr);
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 0.5f);
    EXPECT_FLOAT_EQ(he.y, 0.5f);
    EXPECT_FLOAT_EQ(he.z, 0.5f);
}

TEST(StaticColliderTest, HalfExtentsSetterPersists)
{
    StaticColliderComponent sc(nullptr);
    sc.SetHalfExtents({2.0f, 0.25f, 4.0f});
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 2.0f);
    EXPECT_FLOAT_EQ(he.y, 0.25f);
    EXPECT_FLOAT_EQ(he.z, 4.0f);
}

TEST(StaticColliderTest, WorldAABBReflectsOwnerPosition)
{
    GameObject obj;
    StaticColliderComponent sc(&obj, {1.0f, 0.5f, 2.0f});
    obj.Root().SetPosition({10.0f, 3.0f, -5.0f});

    const NS::Core::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 10.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 3.0f);
    EXPECT_FLOAT_EQ(box.Center.z, -5.0f);
    EXPECT_FLOAT_EQ(box.Extents.x, 1.0f);
    EXPECT_FLOAT_EQ(box.Extents.y, 0.5f);
    EXPECT_FLOAT_EQ(box.Extents.z, 2.0f);
}

TEST(StaticColliderTest, WorldAABBWithoutOwnerIsOriginCentered)
{
    StaticColliderComponent sc(nullptr, NS::Core::Vector3{1.0f, 1.0f, 1.0f});
    const NS::Core::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.z, 0.0f);
}
