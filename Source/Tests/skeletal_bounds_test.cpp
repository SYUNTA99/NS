#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/SkeletalMesh.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Graphics::BoneSphere;
    using NS::Graphics::ComputeBoneSpheres;
    using NS::Graphics::MergeSkinnedBounds;
    using NS::Graphics::SkinnedVertex;
    using NS::Core::AABB;
    using NS::Core::Matrix;
    using NS::Core::Vector3;

    SkinnedVertex MakeVertex(const Vector3& pos, std::uint32_t joint)
    {
        SkinnedVertex v{};
        v.position = pos;
        v.normal = Vector3{0.0f, 0.0f, 1.0f};
        v.joints[0] = joint;
        v.weights[0] = 1.0f;
        return v;
    }

    // 単一パレットでの1頂点スキニング。全影響ボーンの重み付き和で、実描画のスキニングと同じ式
    Vector3 SkinVertex(const SkinnedVertex& v, const std::vector<Matrix>& palette)
    {
        Vector3 acc{0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k)
        {
            if (v.weights[k] <= 0.0f)
                continue;
            acc += Vector3::Transform(v.position, palette[v.joints[k]]) * v.weights[k];
        }
        return acc;
    }
} // namespace

TEST(SkeletalBounds, ComputeBoneSpheresCentersAndRadii)
{
    const std::vector<SkinnedVertex> verts = {
        MakeVertex(Vector3{0.0f, 0.0f, 0.0f}, 0),
        MakeVertex(Vector3{2.0f, 0.0f, 0.0f}, 0),
        MakeVertex(Vector3{0.0f, 10.0f, 0.0f}, 1),
        MakeVertex(Vector3{0.0f, 12.0f, 0.0f}, 1),
    };
    const std::vector<BoneSphere> spheres = ComputeBoneSpheres(verts.data(), verts.size(), 3);

    ASSERT_EQ(spheres.size(), 3u);
    EXPECT_NEAR(spheres[0].center.x, 1.0f, 1e-4f);
    EXPECT_NEAR(spheres[0].center.y, 0.0f, 1e-4f);
    EXPECT_NEAR(spheres[0].radius, 1.0f, 1e-4f);
    EXPECT_NEAR(spheres[1].center.y, 11.0f, 1e-4f);
    EXPECT_NEAR(spheres[1].radius, 1.0f, 1e-4f);
    // 影響頂点の無いボーンは半径が負
    EXPECT_LT(spheres[2].radius, 0.0f);
}

TEST(SkeletalBounds, MergeFollowsPose)
{
    const std::vector<SkinnedVertex> verts = {
        MakeVertex(Vector3{0.0f, 10.0f, 0.0f}, 0),
        MakeVertex(Vector3{0.0f, 12.0f, 0.0f}, 0),
    };
    const std::vector<BoneSphere> spheres = ComputeBoneSpheres(verts.data(), verts.size(), 1);
    const AABB fallback{};

    // 恒等パレットではバインド箱の中心に締まる
    std::vector<Matrix> palette = {Matrix::Identity};
    const AABB bind = MergeSkinnedBounds(spheres, palette.data(), palette.size(), fallback);
    EXPECT_NEAR(bind.Center.y, 11.0f, 1e-3f);

    // ボーンを上へ平行移動すると境界も追従して上がる
    palette[0] = Matrix::CreateTranslation(0.0f, 5.0f, 0.0f);
    const AABB moved = MergeSkinnedBounds(spheres, palette.data(), palette.size(), fallback);
    EXPECT_GT(moved.Center.y, bind.Center.y + 4.0f);
}

TEST(SkeletalBounds, NeverUnderCoversSkinnedVertices)
{
    std::vector<SkinnedVertex> verts;
    for (int i = 0; i < 8; ++i)
    {
        SkinnedVertex v{};
        v.position = Vector3{static_cast<float>(i) - 3.5f, static_cast<float>(i % 3), 0.5f};
        v.normal = Vector3{0.0f, 0.0f, 1.0f};
        v.joints[0] = 0;
        v.joints[1] = 1;
        v.weights[0] = 0.6f;
        v.weights[1] = 0.4f;
        verts.push_back(v);
    }
    const std::vector<BoneSphere> spheres = ComputeBoneSpheres(verts.data(), verts.size(), 2);

    const std::vector<Matrix> palette = {
        Matrix::CreateRotationZ(0.9f) * Matrix::CreateTranslation(1.0f, -2.0f, 0.5f),
        Matrix::CreateRotationY(1.3f) * Matrix::CreateTranslation(-3.0f, 4.0f, 1.0f),
    };
    const AABB fallback{};
    const AABB merged = MergeSkinnedBounds(spheres, palette.data(), palette.size(), fallback);

    // どのスキン後頂点も境界の中に入る。境界が小さすぎると画面端で手足が消える
    for (const SkinnedVertex& v : verts)
    {
        const Vector3 p = SkinVertex(v, palette);
        const DirectX::ContainmentType c = merged.Contains(DirectX::XMLoadFloat3(&p));
        EXPECT_NE(c, DirectX::DISJOINT);
    }
}

TEST(SkeletalBounds, EmptyReturnsFallback)
{
    const std::vector<BoneSphere> spheres = ComputeBoneSpheres(nullptr, 0, 2);
    const std::vector<Matrix> palette = {Matrix::Identity, Matrix::Identity};
    const AABB fallback{DirectX::XMFLOAT3{1.0f, 2.0f, 3.0f}, DirectX::XMFLOAT3{4.0f, 5.0f, 6.0f}};
    const AABB result = MergeSkinnedBounds(spheres, palette.data(), palette.size(), fallback);

    // 影響球がゼロなので fallback がそのまま返る
    EXPECT_NEAR(result.Center.x, 1.0f, 1e-4f);
    EXPECT_NEAR(result.Extents.y, 5.0f, 1e-4f);
}
