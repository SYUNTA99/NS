#include <gtest/gtest.h>

#include <Framework/Graphics/Skeleton.h>
#include <Framework/Graphics/detail/gltf_skin_helpers.h>
#include <Framework/Math/Math.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using NS::Graphics::Bone;
    using NS::Graphics::detail::ConjugateZMatrix;
    using NS::Graphics::detail::MirrorQuaternionZ;
    using NS::Graphics::detail::MirrorZ;
    using NS::Graphics::detail::NormalizeJointWeights;
    using NS::Graphics::detail::ReadColumnMajorMatrix;
    using NS::Graphics::detail::TopologicalSortBones;
    using NS::Math::Matrix;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;

    constexpr float kEps = 1e-5f;
} // namespace

TEST(GltfSkinHelperTest, NormalizeWeightsSumsToOne)
{
    std::array<std::uint32_t, 4> joints{0, 1, 2, 3};
    std::array<float, 4> weights{1.0f, 1.0f, 0.0f, 0.0f};
    NormalizeJointWeights(joints, weights);
    EXPECT_NEAR(weights[0], 0.5f, kEps);
    EXPECT_NEAR(weights[1], 0.5f, kEps);
    EXPECT_NEAR(weights[2], 0.0f, kEps);
    EXPECT_NEAR(weights[3], 0.0f, kEps);
}

TEST(GltfSkinHelperTest, NormalizeAllZeroWeightsFallsBackToFirstBone)
{
    std::array<std::uint32_t, 4> joints{7, 8, 9, 10};
    std::array<float, 4> weights{0.0f, 0.0f, 0.0f, 0.0f};
    NormalizeJointWeights(joints, weights);
    EXPECT_EQ(joints[0], 0u);
    EXPECT_NEAR(weights[0], 1.0f, kEps);
    EXPECT_NEAR(weights[1], 0.0f, kEps);
    EXPECT_NEAR(weights[2], 0.0f, kEps);
    EXPECT_NEAR(weights[3], 0.0f, kEps);
}

TEST(GltfSkinHelperTest, MirrorZNegatesZComponent)
{
    const Vector3 r = MirrorZ(Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(r.x, 1.0f, kEps);
    EXPECT_NEAR(r.y, 2.0f, kEps);
    EXPECT_NEAR(r.z, -3.0f, kEps);
}

TEST(GltfSkinHelperTest, MirrorQuaternionZNegatesXY)
{
    const Quaternion r = MirrorQuaternionZ(Quaternion(0.1f, 0.2f, 0.3f, 0.4f));
    EXPECT_NEAR(r.x, -0.1f, kEps);
    EXPECT_NEAR(r.y, -0.2f, kEps);
    EXPECT_NEAR(r.z, 0.3f, kEps);
    EXPECT_NEAR(r.w, 0.4f, kEps);
}

TEST(GltfSkinHelperTest, ReadColumnMajorMatrixPlacesTranslationInRow3)
{
    // 列優先 glTF 行列の translation は列 3 (index 12,13,14)
    const float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 5.0f, 6.0f, 7.0f, 1.0f};
    const Matrix n = ReadColumnMajorMatrix(m);
    // 行ベクトル規約では translation は _41/_42/_43 に入る
    EXPECT_NEAR(n._41, 5.0f, kEps);
    EXPECT_NEAR(n._42, 6.0f, kEps);
    EXPECT_NEAR(n._43, 7.0f, kEps);
    const Vector3 origin = Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), n);
    EXPECT_NEAR(origin.x, 5.0f, kEps);
    EXPECT_NEAR(origin.y, 6.0f, kEps);
    EXPECT_NEAR(origin.z, 7.0f, kEps);
}

TEST(GltfSkinHelperTest, ConjugateZMatrixFlipsTranslationZ)
{
    const Matrix conjugated = ConjugateZMatrix(Matrix::CreateTranslation(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(conjugated._41, 1.0f, kEps);
    EXPECT_NEAR(conjugated._42, 2.0f, kEps);
    EXPECT_NEAR(conjugated._43, -3.0f, kEps);
    const Vector3 origin = Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), conjugated);
    EXPECT_NEAR(origin.z, -3.0f, kEps);
}

TEST(GltfSkinHelperTest, TopologicalSortPutsParentsBeforeChildren)
{
    // 入力は index0=child(parent=1), index1=root(parent=-1) で子が先
    std::vector<Bone> bones(2);
    bones[0].parentIndex = 1;
    bones[1].parentIndex = -1;
    const std::vector<std::uint32_t> remap = TopologicalSortBones(bones);

    ASSERT_EQ(bones.size(), 2u);
    EXPECT_EQ(bones[0].parentIndex, -1); // root が先頭
    EXPECT_EQ(bones[1].parentIndex, 0);  // child の親は先頭の root
    EXPECT_EQ(remap[0], 1u);             // old child -> new index 1
    EXPECT_EQ(remap[1], 0u);             // old root  -> new index 0
}
