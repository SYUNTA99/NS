#include <gtest/gtest.h>

#include <Framework/Graphics/SkeletalMesh.h>
#include <Framework/Graphics/Skeleton.h>
#include <Framework/Math/Math.h>

#include <array>
#include <span>
#include <vector>

namespace
{
    using NS::Graphics::Bone;
    using NS::Graphics::BonePose;
    using NS::Graphics::Skeleton;
    using NS::Graphics::SkinnedVertex;
    using NS::Math::Matrix;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;

    constexpr float kEps = 1e-4f;

    void ExpectVec3Near(const Vector3& actual, const Vector3& expected, float eps = kEps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    void ExpectMatrixNearIdentity(const Matrix& m, float eps = kEps)
    {
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                float expected = 0.0f;
                if (r == c)
                    expected = 1.0f;
                EXPECT_NEAR(m.m[r][c], expected, eps) << "m[" << r << "][" << c << "]";
            }
        }
    }

    Quaternion RotZ(float degrees)
    {
        return Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), NS::Math::DegreesToRadians(degrees));
    }
} // namespace

TEST(SkeletonTest, ComputeBindPaletteIsIdentityForWellFormedSkin)
{
    std::vector<Bone> bones(2);
    bones[0].parentIndex = -1;
    bones[0].bindLocal.translation = Vector3(2.0f, 0.0f, 0.0f);
    bones[1].parentIndex = 0;
    bones[1].bindLocal.translation = Vector3(0.0f, 3.0f, 0.0f);

    // bind world を local 合成 (scale*rotate*translate, child = local*parentWorld) で求め、 逆を invBind に置く
    const Matrix world0 = Matrix::CreateScale(bones[0].bindLocal.scale) *
                          Matrix::CreateFromQuaternion(bones[0].bindLocal.rotation) *
                          Matrix::CreateTranslation(bones[0].bindLocal.translation);
    const Matrix local1 = Matrix::CreateScale(bones[1].bindLocal.scale) *
                          Matrix::CreateFromQuaternion(bones[1].bindLocal.rotation) *
                          Matrix::CreateTranslation(bones[1].bindLocal.translation);
    const Matrix world1 = local1 * world0;
    bones[0].inverseBind = world0.Invert();
    bones[1].inverseBind = world1.Invert();

    Skeleton skeleton(std::move(bones));
    std::vector<Matrix> palette;
    skeleton.ComputeBindPalette(palette);

    ASSERT_EQ(palette.size(), 2u);
    ExpectMatrixNearIdentity(palette[0]);
    ExpectMatrixNearIdentity(palette[1]);
}

TEST(SkeletonTest, ComputePaletteAppliesSingleBoneRotation)
{
    std::vector<Bone> bones(1);
    bones[0].parentIndex = -1; // invBind は既定の恒等
    Skeleton skeleton(std::move(bones));

    std::array<BonePose, 1> pose{};
    pose[0].rotation = RotZ(90.0f);

    std::vector<Matrix> palette;
    skeleton.ComputePalette(std::span<const BonePose>(pose.data(), pose.size()), palette);
    ASSERT_EQ(palette.size(), 1u);

    // +90°Z 回転で (1,0,0) -> (0,1,0)
    const Vector3 rotated = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), palette[0]);
    ExpectVec3Near(rotated, Vector3(0.0f, 1.0f, 0.0f));
}

TEST(SkeletonTest, ComputePaletteComposesParentChildWorld)
{
    std::vector<Bone> bones(2);
    bones[0].parentIndex = -1;
    bones[1].parentIndex = 0; // invBind は両方とも既定の恒等
    Skeleton skeleton(std::move(bones));

    std::array<BonePose, 2> pose{};
    pose[0].translation = Vector3(2.0f, 0.0f, 0.0f); // 親
    pose[1].translation = Vector3(0.0f, 3.0f, 0.0f); // 子 (親に相対)

    std::vector<Matrix> palette;
    skeleton.ComputePalette(std::span<const BonePose>(pose.data(), pose.size()), palette);
    ASSERT_EQ(palette.size(), 2u);

    // 子 world = T(0,3,0) * T(2,0,0) = T(2,3,0)。 原点を変換すると (2,3,0)
    const Vector3 childOrigin = Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), palette[1]);
    ExpectVec3Near(childOrigin, Vector3(2.0f, 3.0f, 0.0f));
}

TEST(SkeletonTest, SkinPositionReferenceFullWeightSingleBone)
{
    std::array<Matrix, 1> palette{Matrix::CreateFromQuaternion(RotZ(90.0f))};

    SkinnedVertex vertex{};
    vertex.position = Vector3(1.0f, 0.0f, 0.0f);
    vertex.weights[0] = 1.0f;

    const Vector3 skinned =
        Skeleton::SkinPositionReference(vertex, std::span<const Matrix>(palette.data(), palette.size()));
    ExpectVec3Near(skinned, Vector3(0.0f, 1.0f, 0.0f));
}

TEST(SkeletonTest, SkinPositionReferenceBlendsTwoBones)
{
    std::array<Matrix, 2> palette{Matrix::Identity, Matrix::CreateFromQuaternion(RotZ(90.0f))};

    SkinnedVertex vertex{};
    vertex.position = Vector3(1.0f, 0.0f, 0.0f);
    vertex.joints[0] = 0;
    vertex.joints[1] = 1;
    vertex.weights[0] = 0.5f;
    vertex.weights[1] = 0.5f;

    // 0.5*(1,0,0) + 0.5*(0,1,0) = (0.5,0.5,0)
    const Vector3 skinned =
        Skeleton::SkinPositionReference(vertex, std::span<const Matrix>(palette.data(), palette.size()));
    ExpectVec3Near(skinned, Vector3(0.5f, 0.5f, 0.0f));
}
