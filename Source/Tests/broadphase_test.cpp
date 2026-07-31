#include <gtest/gtest.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Physics/CapsuleMover.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <vector>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Vector3;
    using NS::Physics::CapsuleMover;
    using NS::Physics::CapsuleMoverInput;
    using NS::Physics::CapsuleMoverResult;
    using NS::Physics::PhysicsWorld;

    AABB MakeBox(const Vector3& c, const Vector3& e)
    {
        AABB b;
        b.Center = c;
        b.Extents = e;
        return b;
    }

    std::vector<AABB> MakeScene()
    {
        std::vector<AABB> world;
        world.push_back(MakeBox({0.0f, -0.5f, 0.0f}, {20.0f, 0.5f, 20.0f}));
        for (int x = -8; x <= 8; x += 2)
            for (int z = -8; z <= 8; z += 2)
                world.push_back(MakeBox({static_cast<float>(x), 0.5f, static_cast<float>(z)}, {0.4f, 0.5f, 0.4f}));
        return world;
    }

    // useGrid=true は BuildBroadphase を通した grid 候補、 false は総当たり
    Vector3 RunSim(const std::vector<AABB>& world, bool useGrid)
    {
        PhysicsWorld pw;
        for (const AABB& b : world)
            pw.AddAABB(b);
        if (useGrid)
            pw.BuildBroadphase();

        CapsuleMover cc;
        Vector3 pos{0.0f, 3.0f, 0.0f};
        Vector3 vel{4.0f, 0.0f, 2.0f};
        for (int i = 0; i < 120; ++i)
        {
            vel.y -= 20.0f * (1.0f / 60.0f);
            CapsuleMoverInput in;
            in.position = pos;
            in.velocity = vel;
            in.dt = 1.0f / 60.0f;
            in.physicsWorld = &pw;
            const CapsuleMoverResult r = cc.Update(in);
            pos = r.position;
            vel = r.velocity;
        }
        return pos;
    }
} // namespace

// grid 候補と総当たりで最終 position が一致する
TEST(BroadphaseTest, GridResultMatchesBruteForce)
{
    const std::vector<AABB> world = MakeScene();

    const Vector3 withGrid = RunSim(world, true);
    const Vector3 bruteForce = RunSim(world, false);

    EXPECT_NEAR(withGrid.x, bruteForce.x, 1e-4f);
    EXPECT_NEAR(withGrid.y, bruteForce.y, 1e-4f);
    EXPECT_NEAR(withGrid.z, bruteForce.z, 1e-4f);
}

// broadphase 未構築でも総当たりへ落ちて床に乗る
TEST(BroadphaseTest, NoBroadphaseFallsBackToBruteForce)
{
    const std::vector<AABB> world = MakeScene();
    const Vector3 pos = RunSim(world, false);
    EXPECT_GT(pos.y, -0.5f);
    EXPECT_LT(pos.y, 2.0f);
}
