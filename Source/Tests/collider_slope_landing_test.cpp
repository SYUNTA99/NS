#include "NSlib/Core/Geometry/MeshPrimitives.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Phys/MeshShape.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "NSlib/Phys/Triangle.h"

#include <gtest/gtest.h>

#include <iterator>
#include <vector>

namespace
{
    constexpr float k_Dt = 1.0f / 60.0f;
    constexpr float k_GravityStep = -35.0f * k_Dt;

    class ColliderSlopeLanding : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const NS::Quaternion tilt =
                NS::Quaternion::CreateFromAxisAngle(NS::Vector3{1.0f, 0.0f, 0.0f}, -NS::k_Pi / 4.0f);
            m_scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 0.0f, 6.0f}, tilt, NS::Vector3{5.0f, 3.0f, 10.0f}),
                                     NS::Phys::ObjectLayers::Terrain);
            m_root.SetPosition(NS::Vector3{0.0f, 8.0f, 6.0f});

            bool landed = false;
            for (int frame = 0; frame < 120 && !landed; ++frame)
            {
                Step(NS::Vector3{0.0f, -8.0f, 0.0f});
                landed = m_body.IsGrounded();
            }
            ASSERT_TRUE(landed);
        }

        void Step(const NS::Vector3& velocity)
        {
            m_body.SetVelocity(velocity);
            m_body.Move(&m_root, &m_collider, k_Dt, 0.0f);
        }

        NS::Scene m_scene;
        NS::Transform m_root;
        NS::Collider m_collider{&m_scene.Physics(), &m_root};
        NS::Body m_body;
    };
} // namespace

TEST_F(ColliderSlopeLanding, FallingOntoAWalkableSlopeDoesNotSlideBack)
{
    const NS::Vector3 velocity = m_body.Velocity();
    EXPECT_NEAR(velocity.z, 0.0f, 1.0e-3f);
    EXPECT_NEAR(velocity.y, 0.0f, 1.0e-3f);
}

TEST_F(ColliderSlopeLanding, StandingStillOnAWalkableSlopeDoesNotSlideDown)
{
    const NS::Vector3 standing = m_root.Position();
    for (int frame = 0; frame < 60; ++frame)
    {
        Step(NS::Vector3{0.0f, k_GravityStep, 0.0f});
    }
    EXPECT_TRUE(m_body.IsGrounded());
    EXPECT_NEAR(m_root.Position().z, standing.z, 1.0e-3f);
    EXPECT_NEAR(m_root.Position().y, standing.y, 1.0e-3f);
}

TEST_F(ColliderSlopeLanding, RunningUpAWalkableSlopeStaysGrounded)
{
    // 登る時に Jolt が返す上向きの速さを次のフレームへ持ち越すと、坂に要る速さとずれて足が浮く
    for (int frame = 0; frame < 15; ++frame)
    {
        NS::Vector3 velocity = m_body.Velocity();
        velocity.y += k_GravityStep;
        velocity.z = 8.0f;
        Step(velocity);
        EXPECT_TRUE(m_body.IsGrounded()) << "frame " << frame;
        EXPECT_NEAR(m_body.Velocity().z, 8.0f, 1.0e-2f) << "frame " << frame;
    }
}

TEST_F(ColliderSlopeLanding, JumpingOffAWalkableSlopeLeavesTheGround)
{
    Step(NS::Vector3{0.0f, 11.0f, 0.0f});
    EXPECT_FALSE(m_body.IsGrounded());
    EXPECT_GT(m_body.Velocity().y, 0.0f);
}

TEST_F(ColliderSlopeLanding, PlacedOnFlatFloorDoesNotFollowTheOldSlope)
{
    // 坂に立ったまま平らな床の上へ置き直す。前の坂の向きで速さを写すと、登る向きの縦の速さが付いて床から浮く
    m_scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{30.0f, -0.5f, 0.0f}, NS::Quaternion::Identity, NS::Vector3{5.0f, 0.5f, 5.0f}),
                             NS::Phys::ObjectLayers::Terrain);
    ASSERT_TRUE(m_body.IsGrounded());
    const float standingY = m_collider.CapsuleHalfHeight() + m_collider.CapsuleRadius();
    m_root.SetPosition(NS::Vector3{30.0f, standingY, 0.0f});

    Step(NS::Vector3{0.0f, k_GravityStep, 8.0f});
    EXPECT_TRUE(m_body.IsGrounded());
    EXPECT_NEAR(m_root.Position().y, standingY, 0.02f);
}

TEST_F(ColliderSlopeLanding, RunningDownAWalkableSlopeStaysGrounded)
{
    // 下る向きの水平の速さだけで進むと、坂の面が足の下から離れて毎フレーム宙に浮く
    for (int frame = 0; frame < 15; ++frame)
    {
        Step(NS::Vector3{0.0f, k_GravityStep, -8.0f});
        EXPECT_TRUE(m_body.IsGrounded()) << "frame " << frame;
    }
}

TEST(ColliderSlopeTop, RunningOverTheTopOfAWalkableSlopeKeepsTheHorizontalSpeed)
{
    // rebound_test.scene の「45度の坂」と床、自機の当たりの大きさ。地形の部品 (MapPartsActor) と同じく、
    // 立方体のメッシュの形を共有して拡縮と回転で置く。箱の形では面の三角形ごとの縁に触れないので、頂上の角で止まらない
    NS::Scene scene;
    const NS::MeshGeometry cube = NS::MakeCube(NS::Vector3{0.5f, 0.5f, 0.5f});
    NS::Phys::MeshShape cubeShape;
    for (std::size_t i = 0; i + 2 < cube.indices.size(); i += 3)
    {
        cubeShape.triangles.push_back(NS::Phys::Triangle{cube.vertices[cube.indices[i]].position,
                                                         cube.vertices[cube.indices[i + 1]].position,
                                                         cube.vertices[cube.indices[i + 2]].position});
    }
    cubeShape.shape = NS::Phys::CreateMeshShape(cubeShape.triangles);
    const auto addCube = [&](const NS::Vector3& center, const NS::Quaternion& rotation, const NS::Vector3& scale) {
        scene.Physics().SyncMeshShape(JPH::BodyID{}, cubeShape, center, rotation, scale, NS::Phys::ObjectLayers::Terrain);
    };
    addCube(NS::Vector3{18.0f, 0.0f, 5.0f}, NS::Quaternion::Identity, NS::Vector3{70.0f, 1.0f, 50.0f});
    addCube(NS::Vector3{12.0f, 0.5f, 5.0f},
            NS::Quaternion{-0.3826833963394165f, 0.0f, 0.0f, 0.9238795042037964f},
            NS::Vector3{6.0f, 1.0f, 10.0f});
    NS::Transform root;
    NS::Collider collider{&scene.Physics(), &root};
    collider.SetCapsuleRadius(0.65f);
    collider.SetStandingHalfHeight(0.5f);
    NS::Body body;

    // ジャンプで坂に着地したフレームから、自機が渡した前へ進む速さ。頂上の縁に触れるかは位置の小さな差で変わるので、
    // Replay で記録した値をそのまま使う
    root.SetPosition(NS::Vector3{12.0f, 3.30287075f, 5.63260317f});
    const float forwardSpeeds[] = {8.0f, 6.22500038f, 5.55833387f, 6.22500038f, 6.89166641f, 7.55833292f};
    constexpr float k_RiseGravityStep = -25.0f * k_Dt;
    float verticalSpeed = 3.1166656f;
    float forwardSpeed = 8.0f;
    for (int frame = 0; frame < 40; ++frame)
    {
        if (frame < static_cast<int>(std::size(forwardSpeeds)))
        {
            forwardSpeed = forwardSpeeds[frame];
        }
        else
        {
            forwardSpeed = 8.0f;
        }
        body.SetVelocity(NS::Vector3{0.0f, verticalSpeed, forwardSpeed});
        body.Move(&root, &collider, k_Dt, 0.25f);
        verticalSpeed = body.Velocity().y + k_RiseGravityStep;
        // 頂上の縁を壁と読むと前へ進む速さが消え、接地が切れたり付いたりして坂の上で止まる
        if (frame >= static_cast<int>(std::size(forwardSpeeds)))
        {
            EXPECT_GT(body.Velocity().z, 7.9f) << "frame " << frame << " z " << root.Position().z;
        }
    }
    EXPECT_GT(root.Position().z, 9.0f);
}

TEST(ColliderLedge, WalkingOffALedgeDoesNotSinkToTheLowerFloor)
{
    NS::Scene scene;
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -0.5f, -5.0f}, NS::Quaternion::Identity, NS::Vector3{5.0f, 0.5f, 5.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -0.8f, 5.0f}, NS::Quaternion::Identity, NS::Vector3{5.0f, 0.5f, 5.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    NS::Transform root;
    NS::Collider collider{&scene.Physics(), &root};
    NS::Body body;
    root.SetPosition(NS::Vector3{0.0f, 1.5f, -1.0f});
    for (int frame = 0; frame < 60 && !body.IsGrounded(); ++frame)
    {
        body.SetVelocity(NS::Vector3{0.0f, -8.0f, 0.0f});
        body.Move(&root, &collider, k_Dt, 0.0f);
    }
    ASSERT_TRUE(body.IsGrounded());
    const float standingY = root.Position().y;

    bool left = false;
    for (int frame = 0; frame < 30 && !left; ++frame)
    {
        body.SetVelocity(NS::Vector3{0.0f, body.Velocity().y + k_GravityStep, 8.0f});
        body.Move(&root, &collider, k_Dt, 0.25f);
        left = !body.IsGrounded();
    }
    ASSERT_TRUE(left);
    EXPECT_GT(root.Position().y, standingY - 0.05f);
}

namespace
{
    int CountAirborneFramesRunningForward(NS::Scene& scene, const NS::Vector3& start)
    {
        NS::Transform root;
        NS::Collider collider{&scene.Physics(), &root};
        NS::Body body;
        root.SetPosition(start);
        for (int frame = 0; frame < 60 && !body.IsGrounded(); ++frame)
        {
            body.SetVelocity(NS::Vector3{0.0f, -8.0f, 0.0f});
            body.Move(&root, &collider, k_Dt, 0.0f);
        }
        EXPECT_TRUE(body.IsGrounded());

        int airborneFrames = 0;
        for (int frame = 0; frame < 60; ++frame)
        {
            body.SetVelocity(NS::Vector3{0.0f, body.Velocity().y + k_GravityStep, 8.0f});
            body.Move(&root, &collider, k_Dt, 0.25f);
            if (!body.IsGrounded())
            {
                ++airborneFrames;
            }
        }
        return airborneFrames;
    }
} // namespace

TEST(ColliderStep, RunningUpA25cmStepStaysGrounded)
{
    NS::Scene scene;
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -0.5f, -5.0f}, NS::Quaternion::Identity, NS::Vector3{5.0f, 0.5f, 5.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -0.25f, 5.0f}, NS::Quaternion::Identity, NS::Vector3{5.0f, 0.5f, 5.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    EXPECT_EQ(CountAirborneFramesRunningForward(scene, NS::Vector3{0.0f, 1.5f, -3.0f}), 0);
}

TEST(ColliderSeam, RunningOverSeamsBetweenFloorBoxesStaysGrounded)
{
    // 別の物体どうしの継ぎ目は、Jolt がメッシュの内側の縁として取り除かない
    NS::Scene scene;
    for (int i = 0; i < 6; ++i)
    {
        scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -0.5f, -5.0f + 2.0f * static_cast<float>(i)},
                                           NS::Quaternion::Identity,
                                           NS::Vector3{5.0f, 0.5f, 1.0f}),
                               NS::Phys::ObjectLayers::Terrain);
    }
    EXPECT_EQ(CountAirborneFramesRunningForward(scene, NS::Vector3{0.0f, 1.5f, -5.0f}), 0);
}

TEST(ColliderSlopeTop, RunningUpOntoAPlateauStaysGrounded)
{
    // 坂に沿う上向きの速さを持ったまま台へ出るので、戻さないと台の上へ飛び出す
    NS::Scene scene;
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -0.5f, -5.0f}, NS::Quaternion::Identity, NS::Vector3{5.0f, 0.5f, 5.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    const std::vector<NS::Phys::Triangle> slopeAndPlateau{
        NS::Phys::Triangle{NS::Vector3{-5.0f, 0.0f, 0.0f}, NS::Vector3{-5.0f, 2.0f, 4.0f}, NS::Vector3{5.0f, 2.0f, 4.0f}},
        NS::Phys::Triangle{NS::Vector3{-5.0f, 0.0f, 0.0f}, NS::Vector3{5.0f, 2.0f, 4.0f}, NS::Vector3{5.0f, 0.0f, 0.0f}},
        NS::Phys::Triangle{NS::Vector3{-5.0f, 2.0f, 4.0f}, NS::Vector3{-5.0f, 2.0f, 12.0f}, NS::Vector3{5.0f, 2.0f, 12.0f}},
        NS::Phys::Triangle{NS::Vector3{-5.0f, 2.0f, 4.0f}, NS::Vector3{5.0f, 2.0f, 12.0f}, NS::Vector3{5.0f, 2.0f, 4.0f}}};
    scene.Physics().AddMesh(slopeAndPlateau, NS::Phys::ObjectLayers::Terrain);
    EXPECT_EQ(CountAirborneFramesRunningForward(scene, NS::Vector3{0.0f, 1.5f, -2.0f}), 0);
}
