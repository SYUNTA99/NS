#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <gtest/gtest.h>

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
            m_scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 0.0f, 6.0f}, tilt, NS::Vector3{5.0f, 3.0f, 5.0f}),
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

TEST_F(ColliderSlopeLanding, RunningDownAWalkableSlopeStaysGrounded)
{
    // 下る向きの水平の速さだけで進むと、坂の面が足の下から離れて毎フレーム宙に浮く
    for (int frame = 0; frame < 15; ++frame)
    {
        Step(NS::Vector3{0.0f, k_GravityStep, -8.0f});
        EXPECT_TRUE(m_body.IsGrounded()) << "frame " << frame;
    }
}
