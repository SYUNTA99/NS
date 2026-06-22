#include <gtest/gtest.h>

#include <Framework/Physics/SweptOBB.h>
#include <Framework/Scene/Components/BoxColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

#include <cmath>

namespace
{
    using NS::Scene::GameObject;
    using NS::Scene::BoxColliderComponent;
} // namespace

TEST(BoxColliderTest, DefaultHalfExtentsAreHalfMeterCube)
{
    BoxColliderComponent sc;
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 0.5f);
    EXPECT_FLOAT_EQ(he.y, 0.5f);
    EXPECT_FLOAT_EQ(he.z, 0.5f);
}

TEST(BoxColliderTest, HalfExtentsSetterPersists)
{
    BoxColliderComponent sc;
    sc.SetHalfExtents({2.0f, 0.25f, 4.0f});
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 2.0f);
    EXPECT_FLOAT_EQ(he.y, 0.25f);
    EXPECT_FLOAT_EQ(he.z, 4.0f);
}

TEST(BoxColliderTest, WorldAABBReflectsOwnerPosition)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{1.0f, 0.5f, 2.0f});
    obj.Root().SetPosition({10.0f, 3.0f, -5.0f});

    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 10.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 3.0f);
    EXPECT_FLOAT_EQ(box.Center.z, -5.0f);
    EXPECT_FLOAT_EQ(box.Extents.x, 1.0f);
    EXPECT_FLOAT_EQ(box.Extents.y, 0.5f);
    EXPECT_FLOAT_EQ(box.Extents.z, 2.0f);
}

TEST(BoxColliderTest, WorldAABBWithoutOwnerIsOriginCentered)
{
    BoxColliderComponent sc(NS::Math::Vector3{1.0f, 1.0f, 1.0f});
    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.z, 0.0f);
}

// 自由配置物の非一様 scale が当たりの extents へ反映される (grid と同経路で処理する保証)
TEST(BoxColliderTest, WorldAABBReflectsOwnerScale)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetPosition({10.0f, 0.0f, 0.0f});
    obj.Root().SetScale({4.0f, 2.0f, 6.0f});

    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 10.0f);
    EXPECT_FLOAT_EQ(box.Extents.x, 2.0f); // 0.5 * 4
    EXPECT_FLOAT_EQ(box.Extents.y, 1.0f); // 0.5 * 2
    EXPECT_FLOAT_EQ(box.Extents.z, 3.0f); // 0.5 * 6
}

// 立方体を Y 軸 90° 回しても内包 AABB の extents は元と一致する
TEST(BoxColliderTest, WorldAABBNinetyDegreeYawKeepsCubeExtents)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::kPi * 0.5f, 0.0f, 0.0f));

    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_NEAR(box.Extents.x, 0.5f, 1e-4f);
    EXPECT_NEAR(box.Extents.y, 0.5f, 1e-4f);
    EXPECT_NEAR(box.Extents.z, 0.5f, 1e-4f);
}

// owner の位置と halfExtents が OBB に反映される (回転なしは world 軸と一致)
TEST(BoxColliderTest, WorldOBBReflectsOwnerPositionAndExtents)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{1.0f, 0.5f, 2.0f});
    obj.Root().SetPosition({10.0f, 3.0f, -5.0f});

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_FLOAT_EQ(obb.center.x, 10.0f);
    EXPECT_FLOAT_EQ(obb.center.y, 3.0f);
    EXPECT_FLOAT_EQ(obb.center.z, -5.0f);
    EXPECT_NEAR(obb.halfExtents.x, 1.0f, 1e-4f);
    EXPECT_NEAR(obb.halfExtents.y, 0.5f, 1e-4f);
    EXPECT_NEAR(obb.halfExtents.z, 2.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.x, 1.0f, 1e-4f);
    EXPECT_NEAR(obb.axisY.y, 1.0f, 1e-4f);
    EXPECT_NEAR(obb.axisZ.z, 1.0f, 1e-4f);
}

// 非一様 scale が OBB の halfExtents へ乗る
TEST(BoxColliderTest, WorldOBBReflectsOwnerScale)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetScale({4.0f, 2.0f, 6.0f});

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.halfExtents.x, 2.0f, 1e-4f);
    EXPECT_NEAR(obb.halfExtents.y, 1.0f, 1e-4f);
    EXPECT_NEAR(obb.halfExtents.z, 3.0f, 1e-4f);
}

// Y 軸 90° 回転で OBB 軸が world X と直交する (AABB と違い回転を畳まない)
TEST(BoxColliderTest, WorldOBBRotationProducesRotatedAxes)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::kPi * 0.5f, 0.0f, 0.0f));

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.axisX.x, 0.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.Dot(obb.axisY), 0.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.Dot(obb.axisX), 1.0f, 1e-4f);
}

// owner 無しは原点・単位回転・素の halfExtents
TEST(BoxColliderTest, WorldOBBWithoutOwnerIsOriginIdentity)
{
    BoxColliderComponent sc(NS::Math::Vector3{1.0f, 1.0f, 1.0f});
    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_FLOAT_EQ(obb.center.x, 0.0f);
    EXPECT_NEAR(obb.halfExtents.x, 1.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.x, 1.0f, 1e-4f);
}

// 当たり箱の中心オフセットが owner 位置へ足された world 中心になる (回転 / scale 無し)
TEST(BoxColliderTest, CenterOffsetShiftsWorldCenter)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetPosition({10.0f, 0.0f, 0.0f});
    sc.SetCenterOffset({0.0f, 2.0f, 0.0f});

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.center.x, 10.0f, 1e-4f);
    EXPECT_NEAR(obb.center.y, 2.0f, 1e-4f);
    EXPECT_NEAR(obb.center.z, 0.0f, 1e-4f);
}

// オフセットは親 local 基準なので owner の scale が乗る
TEST(BoxColliderTest, CenterOffsetScalesWithOwner)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetScale({3.0f, 3.0f, 3.0f});
    sc.SetCenterOffset({1.0f, 0.0f, 0.0f});

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.center.x, 3.0f, 1e-4f); // 1.0 * scale 3
}

// オフセットは親 local 基準なので owner の回転で向きが回る (Y 90° で local +X が world XZ 平面で 90° 回る)
TEST(BoxColliderTest, CenterOffsetRotatesWithOwner)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::kPi * 0.5f, 0.0f, 0.0f));
    sc.SetCenterOffset({1.0f, 0.0f, 0.0f});

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.center.x, 0.0f, 1e-4f);
    EXPECT_NEAR(obb.center.y, 0.0f, 1e-4f);
    EXPECT_NEAR(std::abs(obb.center.z), 1.0f, 1e-4f);
}

// 当たり箱の local 回転だけで OBB 軸が回る (owner は無回転)
TEST(BoxColliderTest, LocalRotationRotatesObbAxes)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<BoxColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    sc.SetLocalRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::kPi * 0.5f, 0.0f, 0.0f));

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.axisX.x, 0.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.Dot(obb.axisX), 1.0f, 1e-4f);
}

// Euler(度) で設定し Euler(度) で読み戻すと往復一致する (Inspector 窓口の往復保証)
TEST(BoxColliderTest, RotationEulerDegreesRoundTrips)
{
    BoxColliderComponent sc;
    sc.SetRotationEulerDegrees({0.0f, 90.0f, 0.0f});
    const NS::Math::Vector3 deg = sc.RotationEulerDegrees();
    EXPECT_NEAR(deg.x, 0.0f, 1e-3f);
    EXPECT_NEAR(deg.y, 90.0f, 1e-3f);
    EXPECT_NEAR(deg.z, 0.0f, 1e-3f);
}
