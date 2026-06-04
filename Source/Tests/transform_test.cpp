#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Scene/Transform.h>

#include <cmath>

namespace
{
    using NS::Math::Matrix;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;
    using NS::Scene::Transform;

    bool MatricesNear(const Matrix& a, const Matrix& b, float eps = 1e-4f)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                if (std::abs(a.m[row][col] - b.m[row][col]) > eps)
                    return false;
            }
        }
        return true;
    }

    bool VectorsNear(const Vector3& a, const Vector3& b, float eps = 1e-4f)
    {
        return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps;
    }
} // namespace

TEST(TransformTest, DefaultIsIdentity)
{
    Transform t;
    EXPECT_TRUE(MatricesNear(t.LocalMatrix(), Matrix::Identity));
    EXPECT_TRUE(MatricesNear(t.WorldMatrix(), Matrix::Identity));
    EXPECT_EQ(t.Parent(), nullptr);
    EXPECT_TRUE(t.Children().empty());
}

TEST(TransformTest, SetParentLinksHierarchyAndWorldFollowsParent)
{
    Transform parent;
    Transform child;
    parent.SetPosition({10.0f, 0.0f, 0.0f});
    child.SetPosition({0.0f, 5.0f, 0.0f});

    child.SetParent(&parent);

    EXPECT_EQ(child.Parent(), &parent);
    ASSERT_EQ(parent.Children().size(), std::size_t{1});
    EXPECT_EQ(parent.Children().front(), &child);

    // child.World = child.Local * parent.World
    const Matrix expected = child.LocalMatrix() * parent.WorldMatrix();
    EXPECT_TRUE(MatricesNear(child.WorldMatrix(), expected));
}

TEST(TransformTest, SnapshotStoresPreviousValuesAndCurrentRemainsLatest)
{
    Transform t;
    t.SetPosition({1.0f, 2.0f, 3.0f});
    t.SetScale({2.0f, 2.0f, 2.0f});
    t.Snapshot();

    t.SetPosition({4.0f, 5.0f, 6.0f});
    t.SetScale({3.0f, 3.0f, 3.0f});

    EXPECT_TRUE(VectorsNear(t.PreviousPosition(), {1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(VectorsNear(t.PreviousScale(), {2.0f, 2.0f, 2.0f}));
    EXPECT_TRUE(VectorsNear(t.Position(), {4.0f, 5.0f, 6.0f}));
    EXPECT_TRUE(VectorsNear(t.Scale(), {3.0f, 3.0f, 3.0f}));
}

TEST(TransformTest, InterpolatedLocalMatrixAtAlphaZeroEqualsPrevious)
{
    Transform t;
    t.SetPosition({1.0f, 2.0f, 3.0f});
    t.Snapshot();
    t.SetPosition({10.0f, 20.0f, 30.0f});

    const Matrix atZero = t.InterpolatedLocalMatrix(0.0f);
    const Matrix expectedPrev = Matrix::CreateTranslation({1.0f, 2.0f, 3.0f});
    EXPECT_TRUE(MatricesNear(atZero, expectedPrev));
}

TEST(TransformTest, InterpolatedLocalMatrixAtAlphaOneEqualsCurrent)
{
    Transform t;
    t.SetPosition({1.0f, 2.0f, 3.0f});
    t.Snapshot();
    t.SetPosition({10.0f, 20.0f, 30.0f});

    const Matrix atOne = t.InterpolatedLocalMatrix(1.0f);
    EXPECT_TRUE(MatricesNear(atOne, t.LocalMatrix()));
}

TEST(TransformTest, FreshTransformWithoutSnapshotInterpolatesFromOrigin)
{
    // Snapshot 未実行の新規 Transform は previous が default(原点/単位回転)のまま
    // この状態で alpha<1 を補間すると配置先ではなく原点へ振れる(編集再構築バグの再現)
    Transform t;
    t.SetPosition({10.0f, 20.0f, 30.0f});
    t.SetRotation(Quaternion::CreateFromYawPitchRoll(1.0f, 0.0f, 0.0f));

    const Matrix atZero = t.InterpolatedWorldMatrix(0.0f);
    EXPECT_TRUE(MatricesNear(atZero, Matrix::Identity))
        << "Snapshot 前の previous は default のため alpha=0 で原点行列になるはず";

    const Matrix atHalf = t.InterpolatedWorldMatrix(0.5f);
    EXPECT_FALSE(MatricesNear(atHalf, t.WorldMatrix()))
        << "Snapshot を呼ばない限り中間 alpha は配置先と一致せず原点へ振れる";
}

TEST(TransformTest, SnapshotAfterPlacementStopsOriginSwing)
{
    // 生成直後に配置値で Snapshot しておけば previous==current となり
    // 全 alpha で WorldMatrix と一致して原点へ振れなくなる(修正後の不変条件)
    Transform t;
    t.SetPosition({10.0f, 20.0f, 30.0f});
    t.SetRotation(Quaternion::CreateFromYawPitchRoll(1.0f, 0.0f, 0.0f));
    t.SetScale({2.0f, 2.0f, 2.0f});
    t.Snapshot();

    const Matrix world = t.WorldMatrix();
    EXPECT_TRUE(MatricesNear(t.InterpolatedWorldMatrix(0.0f), world));
    EXPECT_TRUE(MatricesNear(t.InterpolatedWorldMatrix(0.5f), world));
    EXPECT_TRUE(MatricesNear(t.InterpolatedWorldMatrix(1.0f), world));
}

TEST(TransformTest, ChildDestructionDetachesFromParentChildrenList)
{
    Transform parent;
    {
        Transform child;
        child.SetParent(&parent);
        EXPECT_EQ(parent.Children().size(), std::size_t{1});
    }
    EXPECT_TRUE(parent.Children().empty());
}

TEST(TransformTest, ParentDestructionClearsChildParentPointer)
{
    Transform child;
    {
        Transform parent;
        child.SetParent(&parent);
        EXPECT_EQ(child.Parent(), &parent);
    }
    EXPECT_EQ(child.Parent(), nullptr);
}
