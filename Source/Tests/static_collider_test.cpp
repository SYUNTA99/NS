#include <gtest/gtest.h>

#include <Framework/Physics/SweptOBB.h>
#include <Framework/Scene/Components/StaticColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Transform.h>

namespace
{
    using NS::Scene::GameObject;
    using NS::Scene::StaticColliderComponent;
} // namespace

TEST(StaticColliderTest, DefaultHalfExtentsAreHalfMeterCube)
{
    StaticColliderComponent sc;
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 0.5f);
    EXPECT_FLOAT_EQ(he.y, 0.5f);
    EXPECT_FLOAT_EQ(he.z, 0.5f);
}

TEST(StaticColliderTest, HalfExtentsSetterPersists)
{
    StaticColliderComponent sc;
    sc.SetHalfExtents({2.0f, 0.25f, 4.0f});
    const auto he = sc.HalfExtents();
    EXPECT_FLOAT_EQ(he.x, 2.0f);
    EXPECT_FLOAT_EQ(he.y, 0.25f);
    EXPECT_FLOAT_EQ(he.z, 4.0f);
}

TEST(StaticColliderTest, WorldAABBReflectsOwnerPosition)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<StaticColliderComponent>(NS::Math::Vector3{1.0f, 0.5f, 2.0f});
    obj.Root().SetPosition({10.0f, 3.0f, -5.0f});

    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 10.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 3.0f);
    EXPECT_FLOAT_EQ(box.Center.z, -5.0f);
    EXPECT_FLOAT_EQ(box.Extents.x, 1.0f);
    EXPECT_FLOAT_EQ(box.Extents.y, 0.5f);
    EXPECT_FLOAT_EQ(box.Extents.z, 2.0f);
}

TEST(StaticColliderTest, WorldAABBWithoutOwnerIsOriginCentered)
{
    StaticColliderComponent sc(NS::Math::Vector3{1.0f, 1.0f, 1.0f});
    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.z, 0.0f);
}

// 自由配置物の非一様 scale が当たりの extents へ反映される (grid と同経路で処理する保証)
TEST(StaticColliderTest, WorldAABBReflectsOwnerScale)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<StaticColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetPosition({10.0f, 0.0f, 0.0f});
    obj.Root().SetScale({4.0f, 2.0f, 6.0f});

    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_FLOAT_EQ(box.Center.x, 10.0f);
    EXPECT_FLOAT_EQ(box.Extents.x, 2.0f); // 0.5 * 4
    EXPECT_FLOAT_EQ(box.Extents.y, 1.0f); // 0.5 * 2
    EXPECT_FLOAT_EQ(box.Extents.z, 3.0f); // 0.5 * 6
}

// 立方体を Y 軸 90° 回しても内包 AABB の extents は元と一致する
TEST(StaticColliderTest, WorldAABBNinetyDegreeYawKeepsCubeExtents)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<StaticColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::kPi * 0.5f, 0.0f, 0.0f));

    const NS::Math::AABB box = sc.WorldAABB();
    EXPECT_NEAR(box.Extents.x, 0.5f, 1e-4f);
    EXPECT_NEAR(box.Extents.y, 0.5f, 1e-4f);
    EXPECT_NEAR(box.Extents.z, 0.5f, 1e-4f);
}

// owner の位置と halfExtents が OBB に反映される (回転なしは world 軸と一致)
TEST(StaticColliderTest, WorldOBBReflectsOwnerPositionAndExtents)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<StaticColliderComponent>(NS::Math::Vector3{1.0f, 0.5f, 2.0f});
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
TEST(StaticColliderTest, WorldOBBReflectsOwnerScale)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<StaticColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetScale({4.0f, 2.0f, 6.0f});

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.halfExtents.x, 2.0f, 1e-4f);
    EXPECT_NEAR(obb.halfExtents.y, 1.0f, 1e-4f);
    EXPECT_NEAR(obb.halfExtents.z, 3.0f, 1e-4f);
}

// Y 軸 90° 回転で OBB 軸が world X と直交する (AABB と違い回転を畳まない)
TEST(StaticColliderTest, WorldOBBRotationProducesRotatedAxes)
{
    GameObject obj;
    auto& sc = *obj.AddComponent<StaticColliderComponent>(NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    obj.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::kPi * 0.5f, 0.0f, 0.0f));

    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_NEAR(obb.axisX.x, 0.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.Dot(obb.axisY), 0.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.Dot(obb.axisX), 1.0f, 1e-4f);
}

// owner 無しは原点・単位回転・素の halfExtents
TEST(StaticColliderTest, WorldOBBWithoutOwnerIsOriginIdentity)
{
    StaticColliderComponent sc(NS::Math::Vector3{1.0f, 1.0f, 1.0f});
    const NS::Physics::OBB obb = sc.WorldOBB();
    EXPECT_FLOAT_EQ(obb.center.x, 0.0f);
    EXPECT_NEAR(obb.halfExtents.x, 1.0f, 1e-4f);
    EXPECT_NEAR(obb.axisX.x, 1.0f, 1e-4f);
}
