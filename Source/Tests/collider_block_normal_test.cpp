#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <gtest/gtest.h>

// 移動を遮った面の法線を Body が覚える。地形で返る向きを決める材料で、遮らなかったフレームは覚えない

namespace
{
    constexpr float k_Dt = 1.0f / 60.0f;
    // 45° の坂を登れる面として扱う上限。ゲームの既定の 40° より緩くし、登れる坂の振る舞いを確かめる
    constexpr float k_WalkableSlopeRadians = NS::k_Pi * 50.0f / 180.0f;

    void AddBox(NS::Scene& scene, const NS::Vector3& center, const NS::Vector3& half)
    {
        NS::OBB box;
        box.center = center;
        box.halfExtentX = half.x;
        box.halfExtentY = half.y;
        box.halfExtentZ = half.z;
        scene.Physics().AddBox(box, NS::Phys::ObjectLayers::Terrain);
    }

    // 立ったカプセル (半径 0.4・半分の高さ 0.5) の根を床から浮かせずに置く高さ
    constexpr float k_StandingRootY = 0.9f;

    struct Mover
    {
        NS::Transform root;
        NS::Collider collider;
        NS::Body body;

        explicit Mover(NS::Scene& scene) : collider(&scene.Physics(), &root) {}

        // 遮った面が出るまで進め、出たフレームの数を返す。出なければ -1
        int MoveUntilBlocked(const NS::Vector3& velocity, int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                body.SetVelocity(velocity);
                body.Move(&root, &collider, k_Dt, 0.0f, k_WalkableSlopeRadians);
                if (body.IsBlocked())
                {
                    return frame;
                }
            }
            return -1;
        }
    };
} // namespace

TEST(ColliderBlockNormal, MovingIntoAWallReportsTheWallNormal)
{
    NS::Scene scene;
    // +Z に正面を向けた壁。面は z = 2
    AddBox(scene, NS::Vector3{0.0f, 3.0f, 2.5f}, NS::Vector3{5.0f, 3.0f, 0.5f});
    Mover mover{scene};
    mover.root.SetPosition(NS::Vector3{0.0f, 3.0f, 0.0f});

    ASSERT_GE(mover.MoveUntilBlocked(NS::Vector3{0.0f, 0.0f, 20.0f}, 30), 0);
    const NS::Vector3 normal = mover.body.BlockNormal();
    EXPECT_NEAR(normal.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(normal.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(normal.z, -1.0f, 1.0e-3f);
}

TEST(ColliderBlockNormal, DashingIntoAFortyFiveDegreeSlopeReportsTheSlopeAndKeepsTheUpwardRebound)
{
    NS::Scene scene;
    // +Z へ上る 45° の坂。上の面の法線は (0, √½, −√½)
    const NS::Quaternion tilt = NS::Quaternion::CreateFromAxisAngle(NS::Vector3{1.0f, 0.0f, 0.0f}, -NS::k_Pi / 4.0f);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 0.0f, 6.0f}, tilt, NS::Vector3{5.0f, 3.0f, 5.0f}),
                           NS::Phys::ObjectLayers::Terrain);
    Mover mover{scene};
    mover.root.SetPosition(NS::Vector3{0.0f, 1.0f, 0.0f});

    ASSERT_GE(mover.MoveUntilBlocked(NS::Vector3{0.0f, 0.0f, 20.0f}, 30), 0);
    const NS::Vector3 normal = mover.body.BlockNormal();
    EXPECT_NEAR(normal.y, 0.7071f, 1.0e-2f);
    EXPECT_NEAR(normal.z, -0.7071f, 1.0e-2f);

    // 反動で真上へ返した次のフレーム。登れる面の接地の補正で上向きの速さを削らない
    mover.body.SetVelocity(NS::Vector3{0.0f, 20.0f, 0.0f});
    mover.body.Move(&mover.root, &mover.collider, k_Dt, 0.0f, k_WalkableSlopeRadians);
    EXPECT_NEAR(mover.body.Velocity().y, 20.0f, 1.0e-3f);
}

TEST(ColliderBlockNormal, SlidingAlongAWallIsNotBlocked)
{
    NS::Scene scene;
    AddBox(scene, NS::Vector3{0.0f, 3.0f, 2.5f}, NS::Vector3{50.0f, 3.0f, 0.5f});
    Mover mover{scene};
    // 壁の面にカプセルの面を付けて置き、面に沿って横へ動かす
    mover.root.SetPosition(NS::Vector3{0.0f, 3.0f, 1.6f});

    EXPECT_EQ(mover.MoveUntilBlocked(NS::Vector3{20.0f, 0.0f, 0.0f}, 30), -1);
}

TEST(ColliderBlockNormal, WalkingFlatOnTheFloorIsNotBlocked)
{
    NS::Scene scene;
    AddBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{50.0f, 0.5f, 50.0f});
    Mover mover{scene};
    mover.root.SetPosition(NS::Vector3{0.0f, k_StandingRootY, 0.0f});

    EXPECT_EQ(mover.MoveUntilBlocked(NS::Vector3{8.0f, 0.0f, 0.0f}, 30), -1);
}
