#include <gtest/gtest.h>

#include <ns/scene/components/static_collider_component.h>
#include <ns/scene/game_object.h>
#include <ns/scene/transform.h>

namespace
{
    using ns::scene::GameObject;
    using ns::scene::StaticColliderComponent;
} // namespace

TEST(StaticColliderTest, DefaultHalfExtentsAreHalfMeterCube)
{
    StaticColliderComponent sc;
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 0.5f);
    EXPECT_FLOAT_EQ(he.y, 0.5f);
    EXPECT_FLOAT_EQ(he.z, 0.5f);
}

TEST(StaticColliderTest, HalfExtentsSetterPersists)
{
    StaticColliderComponent sc;
    sc.SetHalfExtents({2.0f, 0.25f, 4.0f});
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 2.0f);
    EXPECT_FLOAT_EQ(he.y, 0.25f);
    EXPECT_FLOAT_EQ(he.z, 4.0f);
}

TEST(StaticColliderTest, WorldAABBReflectsOwnerPosition)
{
    GameObject obj;
    StaticColliderComponent sc{{1.0f, 0.5f, 2.0f}};
    obj.RegisterComponent(&sc);
    obj.Root().SetPosition({10.0f, 3.0f, -5.0f});

    const ns::core::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 10.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 3.0f);
    EXPECT_FLOAT_EQ(box.Center.z, -5.0f);
    EXPECT_FLOAT_EQ(box.Extents.x, 1.0f);
    EXPECT_FLOAT_EQ(box.Extents.y, 0.5f);
    EXPECT_FLOAT_EQ(box.Extents.z, 2.0f);
}

TEST(StaticColliderTest, WorldAABBWithoutOwnerIsOriginCentered)
{
    StaticColliderComponent sc{{1.0f, 1.0f, 1.0f}};
    const ns::core::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.z, 0.0f);
}
