#include <Runtime/Core/Math.h>
#include <Runtime/Physics/Capsule.h>
#include <Runtime/Physics/JoltWorld.h>
#include <Runtime/Physics/SweptTriangle.h>

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::OBB;
    using NS::Core::Sphere;
    using NS::Core::Vector3;
    using NS::Physics::Capsule;
    using NS::Physics::JoltWorld;
    using NS::Physics::Triangle;
    namespace ObjectLayers = NS::Physics::ObjectLayers;

    OBB MakeAxisAlignedBox(const Vector3& center, float halfX, float halfY, float halfZ)
    {
        OBB box;
        box.center = center;
        box.halfExtentX = halfX;
        box.halfExtentY = halfY;
        box.halfExtentZ = halfZ;
        return box;
    }

    OBB MakeYawedBox(const Vector3& center, float yawRadians)
    {
        OBB box = MakeAxisAlignedBox(center, 1.0f, 2.0f, 3.0f);
        const float cosYaw = std::cos(yawRadians);
        const float sinYaw = std::sin(yawRadians);
        box.axisX = Vector3{cosYaw, 0.0f, -sinYaw};
        box.axisZ = Vector3{sinYaw, 0.0f, cosYaw};
        return box;
    }

    std::vector<Triangle> MakeFloorQuad()
    {
        return {
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, -1.0f}},
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{-1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, 1.0f}},
        };
    }
} // namespace

TEST(JoltBody, StartsEmpty)
{
    JoltWorld world;

    EXPECT_EQ(world.BodyCount(), 0u);
}

TEST(JoltBody, AddBoxCreatesOneBody)
{
    JoltWorld world;

    const JPH::BodyID id =
        world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, AddSphereCreatesOneBody)
{
    JoltWorld world;
    Sphere sphere;
    sphere.center = Vector3{2.0f, 3.0f, 4.0f};
    sphere.radius = 0.5f;

    const JPH::BodyID id = world.AddSphere(sphere, ObjectLayers::Rock);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, AddCapsuleCreatesOneBody)
{
    JoltWorld world;
    Capsule capsule;
    capsule.center = Vector3{0.0f, 2.0f, 0.0f};
    capsule.halfHeight = 0.5f;
    capsule.radius = 0.4f;

    const JPH::BodyID id = world.AddCapsule(capsule, ObjectLayers::Rock);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, AddMeshCreatesOneBodyForManyTriangles)
{
    JoltWorld world;

    const JPH::BodyID id = world.AddMesh(MakeFloorQuad(), ObjectLayers::Terrain);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, EmptyMeshCreatesNoBody)
{
    JoltWorld world;

    const JPH::BodyID id = world.AddMesh(std::vector<Triangle>{}, ObjectLayers::Terrain);

    EXPECT_TRUE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 0u);
}

TEST(JoltBody, RemoveAllBodiesEmptiesTheWorld)
{
    JoltWorld world;
    world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    world.AddBox(MakeAxisAlignedBox(Vector3{4.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    ASSERT_EQ(world.BodyCount(), 2u);

    world.RemoveAllBodies();

    EXPECT_EQ(world.BodyCount(), 0u);
}

TEST(JoltBody, BoxKeepsCenter)
{
    JoltWorld world;
    const Vector3 center{3.0f, 5.0f, -2.0f};

    const JPH::BodyID id = world.AddBox(MakeAxisAlignedBox(center, 1.0f, 2.0f, 3.0f), ObjectLayers::Terrain);

    const Vector3 stored = world.BodyPosition(id);
    EXPECT_NEAR(stored.x, center.x, 1.0e-5f);
    EXPECT_NEAR(stored.y, center.y, 1.0e-5f);
    EXPECT_NEAR(stored.z, center.z, 1.0e-5f);
}

TEST(JoltBody, YawedBoxKeepsItsAxes)
{
    JoltWorld world;
    const float yaw = 0.6f;

    const JPH::BodyID id = world.AddBox(MakeYawedBox(Vector3{0.0f, 0.0f, 0.0f}, yaw), ObjectLayers::Terrain);

    const Vector3 rotatedX = Vector3::Transform(Vector3{1.0f, 0.0f, 0.0f}, world.BodyRotation(id));
    EXPECT_NEAR(rotatedX.x, std::cos(yaw), 1.0e-5f);
    EXPECT_NEAR(rotatedX.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(rotatedX.z, -std::sin(yaw), 1.0e-5f);
}
