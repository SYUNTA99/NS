#include "NSlib/Core/AABB.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/Collider.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"
#include "NSlib/Object/IUse/IUseCollision.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Physics/PhysicsScene.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    // 動く体の当たりと置く当たりを 1 つずつ持つ Actor。根は原点で、球の半径は 0.5
    class CollisionProbe final : public NS::Obj::Actor
    {
    public:
        NS::Obj::Collider* collider = nullptr;
        NS::Obj::SphereCollision* sphere = nullptr;

    protected:
        void Init() override
        {
            collider = CreateSubObj<NS::Obj::Collider>("Collider");
            sphere = CreateSubObj<NS::Obj::SphereCollision>("Sphere");
        }
    };

    // 上面が y = 0 の床
    void AddFloor(NS::Obj::Scene& scene)
    {
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 5.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 5.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    }

    // 床の上面をまたぐ細い柱。崖掴みが縁を探す箱と同じく幅 0
    NS::AABB BandAcrossFloorTop()
    {
        NS::AABB region;
        region.Center = NS::Vector3{2.0f, 0.0f, 2.0f};
        region.Extents = NS::Vector3{0.0f, 0.25f, 0.0f};
        return region;
    }

    bool HitsDownFrom(const NS::Obj::Scene& scene, const NS::Vector3& from)
    {
        float distance = 0.0f;
        return scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance);
    }
} // namespace

TEST(CollisionWindow, OverlapBoxIsEmptyOutsideAScene)
{
    CollisionProbe loose;
    loose.EnsureInit();
    EXPECT_TRUE(NS::Obj::OverlapBoxCollision(loose, BandAcrossFloorTop()).empty());
}

TEST(CollisionWindow, OverlapBoxAnswersLikeThePhysicsScene)
{
    NS::Obj::Scene scene;
    AddFloor(scene);
    CollisionProbe* placed = scene.SpawnTransient<CollisionProbe>();

    const std::vector<NS::AABB> expected = scene.Physics().OverlapBox(BandAcrossFloorTop());
    const std::vector<NS::AABB> actual = NS::Obj::OverlapBoxCollision(*placed, BandAcrossFloorTop());
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

TEST(CollisionWindow, ColliderAnswersWithItsOwnersPhysicsScene)
{
    NS::Obj::Collider bare;
    const NS::Obj::IUseCollision& bareWindow = bare;
    EXPECT_EQ(bareWindow.GetPhysicsScene(), nullptr);

    CollisionProbe loose;
    loose.EnsureInit();
    const NS::Obj::IUseCollision& looseWindow = *loose.collider;
    EXPECT_EQ(looseWindow.GetPhysicsScene(), nullptr);

    NS::Obj::Scene scene;
    CollisionProbe* placed = scene.SpawnTransient<CollisionProbe>();
    const NS::Obj::IUseCollision& placedWindow = *placed->collider;
    EXPECT_EQ(placedWindow.GetPhysicsScene(), &scene.Physics());
}

TEST(CollisionWindow, CollisionSyncsIntoItsOwnersPhysicsScene)
{
    CollisionProbe loose;
    loose.EnsureInit();
    loose.sphere->SyncToPhysics();
    EXPECT_TRUE(loose.sphere->BodyId().IsInvalid());

    NS::Obj::Scene scene;
    CollisionProbe* placed = scene.SpawnTransient<CollisionProbe>();
    const NS::Vector3 above{0.0f, 2.0f, 0.0f};
    placed->sphere->RemoveFromPhysics();
    EXPECT_TRUE(placed->sphere->BodyId().IsInvalid());
    EXPECT_FALSE(HitsDownFrom(scene, above));

    placed->sphere->SyncToPhysics();
    EXPECT_FALSE(placed->sphere->BodyId().IsInvalid());
    EXPECT_TRUE(HitsDownFrom(scene, above));
}
