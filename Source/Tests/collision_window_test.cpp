#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{
    // 動く体の当たりと置く当たりを 1 つずつ持つ SceneActor。根は原点で、球の半径は 0.5
    class CollisionProbeActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(CollisionProbeActor, NS::SceneActor)
        NS_CLASSOBJ(collider, "Collider")
        NS_CLASSOBJ(sphere, "Sphere")
        NS_REFLECT_END()

        std::unique_ptr<NS::Collider> collider;
        std::unique_ptr<NS::SphereCollision> sphere;

    protected:
        void OnInit() override
        {
            CreateClassObj(collider, GetPhysicsScene(), &Root());
            CreateClassObj(sphere);
        }
    };

    bool HitsDownFrom(const NS::Scene& scene, const NS::Vector3& from)
    {
        float distance = 0.0f;
        return scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance);
    }
} // namespace

TEST(CollisionWindow, CapsuleOverlapIsFalseOutsideAScene)
{
    const std::unique_ptr<CollisionProbeActor> looseOwned = NS::CreateActor<CollisionProbeActor>();
    const NS::Phys::Capsule capsule{.center = NS::Vector3{0.0f, 0.0f, 0.0f}, .halfHeight = 0.5f, .radius = 0.5f};
    EXPECT_FALSE(NS::OverlapsCapsuleCollision(looseOwned->collider.get(), capsule));
}

// 45 度回した箱の角の外接箱には入るが、本当の形には入らない所を見分ける
TEST(CollisionWindow, CapsuleOverlapUsesTheRealShapeOfATurnedBox)
{
    NS::Scene scene;
    const NS::Quaternion turn = NS::Quaternion::CreateFromAxisAngle(NS::Vector3{0.0f, 1.0f, 0.0f}, NS::k_Pi / 4.0f);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 0.0f, 0.0f}, turn, NS::Vector3{1.0f, 1.0f, 1.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    CollisionProbeActor* placed = scene.SpawnActor<CollisionProbeActor>("CollisionProbeActor");
    // 外接箱の角 (1.414, 1.414) の近くは、回した箱の面から 1 m ほど離れている
    const NS::Phys::Capsule nearTheBoundsCorner{.center = NS::Vector3{1.2f, 0.0f, 1.2f}, .halfHeight = 0.2f, .radius = 0.2f};
    const NS::Phys::Capsule acrossTheFace{.center = NS::Vector3{1.3f, 0.0f, 0.0f}, .halfHeight = 0.2f, .radius = 0.2f};
    EXPECT_FALSE(NS::OverlapsCapsuleCollision(placed->collider.get(), nearTheBoundsCorner));
    EXPECT_TRUE(NS::OverlapsCapsuleCollision(placed->collider.get(), acrossTheFace));
}

TEST(CollisionWindow, CapsuleCastFindsTheFacingWallAndSkipsTheFloorItTouches)
{
    NS::Scene scene;
    // 奥の壁の面は z = 5。床の上の面は y = -0.4 で、投げる球の底が触れている
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 0.0f, 6.0f}, NS::Quaternion::Identity, NS::Vector3{3.0f, 3.0f, 1.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -1.4f, 0.0f}, NS::Quaternion::Identity, NS::Vector3{8.0f, 1.0f, 8.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    CollisionProbeActor* placed = scene.SpawnActor<CollisionProbeActor>("CollisionProbeActor");
    const NS::Phys::Capsule ball{.center = NS::Vector3{0.0f, 0.0f, 0.0f}, .halfHeight = 0.0f, .radius = 0.4f};

    NS::Phys::CapsuleCastHit hit{};
    ASSERT_TRUE(NS::CastCapsuleCollision(placed->collider.get(), ball, NS::Vector3{0.0f, 0.0f, 1.0f}, 10.0f, 0.1f, hit));
    EXPECT_NEAR(hit.distance, 4.6f, 1.0e-3f);
    EXPECT_NEAR(hit.normal.z, -1.0f, 1.0e-3f);
    EXPECT_NEAR(hit.point.z, 5.0f, 1.0e-3f);

    NS::Phys::CapsuleCastHit untouched{.distance = 7.0f};
    EXPECT_FALSE(
        NS::CastCapsuleCollision(placed->collider.get(), ball, NS::Vector3{0.0f, 0.0f, 1.0f}, 3.0f, 0.1f, untouched));
    EXPECT_FLOAT_EQ(untouched.distance, 7.0f);
}

TEST(CollisionWindow, CapsuleCastAtABoxEdgeReturnsAFaceNormalLikeTheMovingBody)
{
    // 柱の縦の角の辺に玉の縁で当てる。動く体 (CharacterVirtual) は箱の面の法線を読むので、投げた時も面の法線を返す。
    // 接触の法線 (辺から玉の中心への向き) は (-0.62, 0, -0.78) の斜めで、反動の向きの割り当てが動く体と食い違った
    NS::Scene scene;
    // 柱の左の面は x = 0.4、手前の面は z = 3
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{1.4f, 0.0f, 4.0f}, NS::Quaternion::Identity, NS::Vector3{1.0f, 3.0f, 1.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    CollisionProbeActor* placed = scene.SpawnActor<CollisionProbeActor>("CollisionProbeActor");
    const NS::Phys::Capsule ball{.center = NS::Vector3{0.0f, 0.0f, 0.0f}, .halfHeight = 0.0f, .radius = 0.65f};

    NS::Phys::CapsuleCastHit hit{};
    ASSERT_TRUE(NS::CastCapsuleCollision(placed->collider.get(), ball, NS::Vector3{0.0f, 0.0f, 1.0f}, 10.0f, -1.0f, hit));
    EXPECT_NEAR(hit.normal.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(std::max(std::abs(hit.normal.x), std::abs(hit.normal.z)), 1.0f, 1.0e-3f) << "面の法線は軸にそろう";
}

TEST(CollisionWindow, ColliderAnswersWithItsOwnersPhysicsScene)
{
    NS::Collider bare{nullptr, nullptr};
    const NS::IGetCollision& bareWindow = bare;
    EXPECT_EQ(bareWindow.GetPhysicsScene(), nullptr);

    const std::unique_ptr<CollisionProbeActor> looseOwned = NS::CreateActor<CollisionProbeActor>();
    CollisionProbeActor& loose = *looseOwned;
    const NS::IGetCollision& looseWindow = *loose.collider;
    EXPECT_EQ(looseWindow.GetPhysicsScene(), nullptr);

    NS::Scene scene;
    CollisionProbeActor* placed = scene.SpawnActor<CollisionProbeActor>("CollisionProbeActor");
    const NS::IGetCollision& placedWindow = *placed->collider;
    EXPECT_EQ(placedWindow.GetPhysicsScene(), &scene.Physics());
}

TEST(CollisionWindow, CollisionSyncsIntoItsOwnersPhysicsScene)
{
    const std::unique_ptr<CollisionProbeActor> looseOwned = NS::CreateActor<CollisionProbeActor>();
    CollisionProbeActor& loose = *looseOwned;
    loose.sphere->SyncToPhysics(loose.Root().WorldMatrix(), &loose);
    EXPECT_TRUE(loose.sphere->BodyId().IsInvalid());

    NS::Scene scene;
    CollisionProbeActor* placed = scene.SpawnActor<CollisionProbeActor>("CollisionProbeActor");
    const NS::Vector3 above{0.0f, 2.0f, 0.0f};
    EXPECT_FALSE(HitsDownFrom(scene, above));
    placed->sphere->SyncToPhysics(placed->Root().WorldMatrix(), placed);
    const JPH::BodyID body = placed->sphere->BodyId();
    EXPECT_FALSE(body.IsInvalid());
    EXPECT_TRUE(HitsDownFrom(scene, above));

    // 外しても body は壊さず、戻すと同じ body が当たる
    placed->sphere->RemoveFromPhysics(placed);
    EXPECT_EQ(placed->sphere->BodyId(), body);
    EXPECT_FALSE(HitsDownFrom(scene, above));
    placed->sphere->ReturnToPhysics(placed->Root().WorldMatrix(), placed);
    EXPECT_EQ(placed->sphere->BodyId(), body);
    EXPECT_TRUE(HitsDownFrom(scene, above));
}
