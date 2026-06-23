#include <gtest/gtest.h>

#include <Framework/Scene/Components/CapsuleColliderComponent.h>
#include <Framework/Scene/Components/MeshColliderComponent.h>
#include <Framework/Scene/Components/SphereColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

#include <vector>

namespace
{
    using NS::Math::Vector3;
} // namespace

TEST(SphereColliderTest, DefaultRadiusIsHalfMeter)
{
    NS::Scene::SphereColliderComponent sc;
    EXPECT_FLOAT_EQ(sc.Radius(), 0.5f);

    const NS::Physics::Sphere s = sc.WorldSphere();
    EXPECT_FLOAT_EQ(s.center.x, 0.0f);
    EXPECT_FLOAT_EQ(s.radius, 0.5f);
}

TEST(SphereColliderTest, WorldSphereReflectsOwnerPositionAndScale)
{
    NS::Scene::GameObject obj;
    obj.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    obj.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    auto& sc = *obj.AddComponent<NS::Scene::SphereColliderComponent>(0.5f);

    const NS::Physics::Sphere s = sc.WorldSphere();
    EXPECT_FLOAT_EQ(s.center.x, 1.0f);
    EXPECT_FLOAT_EQ(s.center.y, 2.0f);
    EXPECT_FLOAT_EQ(s.center.z, 3.0f);
    EXPECT_FLOAT_EQ(s.radius, 1.0f); // 0.5 * scale 2
}

TEST(SphereColliderTest, CenterOffsetShiftsAndScalesWithOwner)
{
    NS::Scene::GameObject obj;
    obj.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    auto& sc = *obj.AddComponent<NS::Scene::SphereColliderComponent>(0.5f);
    sc.SetCenterOffset(Vector3{1.0f, 0.0f, 0.0f});

    const NS::Physics::Sphere s = sc.WorldSphere();
    EXPECT_FLOAT_EQ(s.center.x, 2.0f); // offset 1 * scale 2
}

TEST(SphereColliderTest, NegativeRadiusClampsToZero)
{
    NS::Scene::SphereColliderComponent sc;
    sc.SetRadius(-3.0f);
    EXPECT_FLOAT_EQ(sc.Radius(), 0.0f);
}

TEST(CapsuleColliderTest, DefaultsAreVertical)
{
    NS::Scene::CapsuleColliderComponent cc;
    EXPECT_FLOAT_EQ(cc.Radius(), 0.4f);
    EXPECT_FLOAT_EQ(cc.HalfHeight(), 0.5f);

    const NS::Physics::Capsule c = cc.WorldCapsule();
    EXPECT_NEAR(c.axis.x, 0.0f, 1e-5f);
    EXPECT_NEAR(c.axis.y, 1.0f, 1e-5f);
    EXPECT_NEAR(c.axis.z, 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(c.radius, 0.4f);
    EXPECT_FLOAT_EQ(c.halfHeight, 0.5f);
}

TEST(CapsuleColliderTest, WorldCapsuleReflectsOwnerScale)
{
    NS::Scene::GameObject obj;
    obj.Root().SetPosition(Vector3{0.0f, 5.0f, 0.0f});
    obj.Root().SetScale(Vector3{2.0f, 3.0f, 2.0f});
    auto& cc = *obj.AddComponent<NS::Scene::CapsuleColliderComponent>(0.4f, 0.5f);

    const NS::Physics::Capsule c = cc.WorldCapsule();
    EXPECT_NEAR(c.center.y, 5.0f, 1e-5f);
    EXPECT_NEAR(c.radius, 0.8f, 1e-5f);     // 0.4 * max(scale.x, scale.z)=2
    EXPECT_NEAR(c.halfHeight, 1.5f, 1e-5f); // 0.5 * scale.y=3
    EXPECT_NEAR(c.axis.y, 1.0f, 1e-5f);
}

TEST(CapsuleColliderTest, RotationEulerDegreesRoundTrips)
{
    NS::Scene::CapsuleColliderComponent cc;
    cc.SetRotationEulerDegrees(Vector3{0.0f, 90.0f, 0.0f});
    EXPECT_NEAR(cc.RotationEulerDegrees().y, 90.0f, 1e-3f);
}

TEST(MeshColliderTest, WorldTrianglesTransformByOwnerPosition)
{
    std::vector<NS::Physics::Triangle> tris = {
        NS::Physics::Triangle{Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}}};

    NS::Scene::GameObject obj;
    obj.Root().SetPosition(Vector3{10.0f, 0.0f, 0.0f});
    auto& cc = *obj.AddComponent<NS::Scene::MeshColliderComponent>(tris);

    const std::vector<NS::Physics::Triangle> world = cc.WorldTriangles();
    ASSERT_EQ(world.size(), 1u);
    EXPECT_NEAR(world[0].v0.x, 10.0f, 1e-5f);
    EXPECT_NEAR(world[0].v1.x, 11.0f, 1e-5f);
    EXPECT_NEAR(world[0].v2.x, 10.0f, 1e-5f);
    EXPECT_NEAR(world[0].v2.y, 1.0f, 1e-5f);
}

TEST(MeshColliderTest, WithoutOwnerReturnsLocalUnchanged)
{
    std::vector<NS::Physics::Triangle> tris = {
        NS::Physics::Triangle{Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}}};
    NS::Scene::MeshColliderComponent cc(tris);

    const std::vector<NS::Physics::Triangle> world = cc.WorldTriangles();
    ASSERT_EQ(world.size(), 1u);
    EXPECT_FLOAT_EQ(world[0].v1.x, 1.0f);
    EXPECT_EQ(cc.LocalTriangles().size(), 1u);
}
