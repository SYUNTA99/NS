#include "Runtime/Core/AABB.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/IUse/IUseCollision.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    // 動く体と出す当たりを 1 つずつ持つ Actor。根は原点で、球の半径は 0.5
    class CollisionProbe final : public NS::Obj::Actor
    {
    public:
        CollisionProbe()
        {
            AttachFixedComponent(body);
            AttachFixedComponent(sphere);
        }
        void ForEachPart(const PartVisitor& visitor) const override
        {
            NS::Obj::Actor::ForEachPart(visitor);
            visitor("Body", body);
            visitor("Sphere", sphere);
        }
        mutable NS::Obj::Body body;
        mutable NS::Obj::SphereCollider sphere;
    };

    // 上面が y = 0 の床
    void AddFloor(NS::Obj::Scene& scene)
    {
        NS::Core::OBB floor{};
        floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 5.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 5.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    }

    // 床の上面をまたぐ細い柱。崖掴みが縁を探す箱と同じく幅 0
    NS::Core::AABB BandAcrossFloorTop()
    {
        NS::Core::AABB region;
        region.Center = NS::Core::Vector3{2.0f, 0.0f, 2.0f};
        region.Extents = NS::Core::Vector3{0.0f, 0.25f, 0.0f};
        return region;
    }

    bool HitsDownFrom(const NS::Obj::Scene& scene, const NS::Core::Vector3& from)
    {
        float distance = 0.0f;
        return scene.Physics().Raycast(from, -NS::Core::Vector3::UnitY, 4.0f, distance);
    }
} // namespace

TEST(CollisionWindow, OverlapBoxIsEmptyOutsideAScene)
{
    CollisionProbe loose;
    EXPECT_TRUE(NS::Obj::OverlapBoxCollision(loose, BandAcrossFloorTop()).empty());
}

TEST(CollisionWindow, OverlapBoxAnswersLikeThePhysicsScene)
{
    NS::Obj::Scene scene;
    AddFloor(scene);
    CollisionProbe* placed = scene.SpawnTransient<CollisionProbe>();

    const std::vector<NS::Core::AABB> expected = scene.Physics().OverlapBox(BandAcrossFloorTop());
    const std::vector<NS::Core::AABB> actual = NS::Obj::OverlapBoxCollision(*placed, BandAcrossFloorTop());
    ASSERT_FALSE(expected.empty());
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_FLOAT_EQ(actual[i].Center.x, expected[i].Center.x);
        EXPECT_FLOAT_EQ(actual[i].Center.y, expected[i].Center.y);
        EXPECT_FLOAT_EQ(actual[i].Center.z, expected[i].Center.z);
        EXPECT_FLOAT_EQ(actual[i].Extents.x, expected[i].Extents.x);
        EXPECT_FLOAT_EQ(actual[i].Extents.y, expected[i].Extents.y);
        EXPECT_FLOAT_EQ(actual[i].Extents.z, expected[i].Extents.z);
    }
}

TEST(CollisionWindow, BodyAnswersWithItsOwnersPhysicsScene)
{
    NS::Obj::Body bare;
    const NS::Obj::IUseCollision& bareWindow = bare;
    EXPECT_EQ(bareWindow.GetPhysicsScene(), nullptr);

    CollisionProbe loose;
    const NS::Obj::IUseCollision& looseWindow = loose.body;
    EXPECT_EQ(looseWindow.GetPhysicsScene(), nullptr);

    NS::Obj::Scene scene;
    CollisionProbe* placed = scene.SpawnTransient<CollisionProbe>();
    const NS::Obj::IUseCollision& placedWindow = placed->body;
    EXPECT_EQ(placedWindow.GetPhysicsScene(), &scene.Physics());
}

TEST(CollisionWindow, ColliderSyncsIntoItsOwnersPhysicsScene)
{
    CollisionProbe loose;
    loose.sphere.SyncToPhysics();
    EXPECT_TRUE(loose.sphere.BodyId().IsInvalid());

    NS::Obj::Scene scene;
    CollisionProbe* placed = scene.SpawnTransient<CollisionProbe>();
    const NS::Core::Vector3 above{0.0f, 2.0f, 0.0f};
    placed->sphere.RemoveFromPhysics();
    EXPECT_TRUE(placed->sphere.BodyId().IsInvalid());
    EXPECT_FALSE(HitsDownFrom(scene, above));

    placed->sphere.SyncToPhysics();
    EXPECT_FALSE(placed->sphere.BodyId().IsInvalid());
    EXPECT_TRUE(HitsDownFrom(scene, above));
}
