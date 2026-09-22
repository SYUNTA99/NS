#include <Runtime/Core/Math.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/PhysicsScene.h>

#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::Quaternion;
    using NS::Core::Vector3;
    using NS::Phys::Triangle;

    std::vector<Triangle> MakeFloorQuad()
    {
        return {
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, -1.0f}},
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{-1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, 1.0f}},
        };
    }
} // namespace

// 三角形群から形ができる。この試しだけを走らせても PhysicsScene 無しで Jolt の下準備が通る
TEST(MeshCollisionTest, CreateMeshShapeBuildsShapeFromTriangles)
{
    const std::vector<Triangle> floor = MakeFloorQuad();
    EXPECT_NE(NS::Phys::CreateMeshShape(floor), nullptr);
}

// 三角形が無ければ形は null
TEST(MeshCollisionTest, EmptyTrianglesMakeNoShape)
{
    EXPECT_EQ(NS::Phys::CreateMeshShape(std::vector<Triangle>{}), nullptr);
}

// 2 x 2 の床を x = 10 へ 3 倍で置くと x = 7〜13 に広がる
// 12.5 は拡縮しなければ外れる所なので当たれば拡縮が効いている。広がりの外の 14 は外れる
TEST(MeshCollisionTest, SyncMeshShapePlacesSharedShapeAtPoseAndScale)
{
    NS::Phys::MeshCollision floor{MakeFloorQuad(), nullptr};
    floor.shape = NS::Phys::CreateMeshShape(floor.triangles);
    ASSERT_NE(floor.shape, nullptr);

    NS::Phys::PhysicsScene physics;
    const JPH::BodyID id = physics.SyncMeshShape(JPH::BodyID{},
                                                 floor,
                                                 Vector3{10.0f, 2.0f, 0.0f},
                                                 Quaternion::Identity,
                                                 Vector3{3.0f, 1.0f, 3.0f},
                                                 NS::Phys::ObjectLayers::Terrain);
    ASSERT_FALSE(id.IsInvalid());

    float distance = 0.0f;
    ASSERT_TRUE(physics.Raycast(Vector3{12.5f, 5.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 8.0f, distance));
    EXPECT_NEAR(distance, 3.0f, 1.0e-3f);
    EXPECT_FALSE(physics.Raycast(Vector3{14.0f, 5.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 8.0f, distance));
}
