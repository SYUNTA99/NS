#include "NSlib/Graphics/SkeletalMesh.h"

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/GraphicObject.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NS::Gfx
{
    std::vector<BoneSphere> ComputeBoneSpheres(const SkinnedVertex* vertices,
                                               std::size_t vertexCount,
                                               std::size_t boneCount)
    {
        // 既定は影響なし。頂点が当たったボーンだけ後で半径を入れる
        std::vector<BoneSphere> spheres(boneCount, BoneSphere{.radius = -1.0f});
        if (vertices == nullptr || vertexCount == 0u || boneCount == 0u)
        {
            return spheres;
        }

        constexpr float k_Big = std::numeric_limits<float>::max();
        std::vector<NS::Vector3> mn(boneCount, NS::Vector3{k_Big, k_Big, k_Big});
        std::vector<NS::Vector3> mx(boneCount, NS::Vector3{-k_Big, -k_Big, -k_Big});
        std::vector<bool> hit(boneCount, false);

        // 各頂点を weight>0 の全影響ボーンへ算入し、ボーンごとの軸並行範囲を取る
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            const SkinnedVertex& v = vertices[i];
            for (int k = 0; k < 4; ++k)
            {
                if (v.weights[k] <= 0.0f)
                {
                    continue;
                }
                const std::uint32_t j = v.joints[k];
                if (j >= boneCount)
                {
                    continue;
                }
                mn[j] = NS::Vector3::Min(mn[j], v.position);
                mx[j] = NS::Vector3::Max(mx[j], v.position);
                hit[j] = true;
            }
        }
        for (std::size_t j = 0; j < boneCount; ++j)
        {
            if (hit[j])
            {
                spheres[j].center = (mn[j] + mx[j]) * 0.5f;
            }
        }

        // 中心が決まってから最遠影響頂点までの距離を半径にする
        std::vector<float> maxD2(boneCount, 0.0f);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            const SkinnedVertex& v = vertices[i];
            for (int k = 0; k < 4; ++k)
            {
                if (v.weights[k] <= 0.0f)
                {
                    continue;
                }
                const std::uint32_t j = v.joints[k];
                if (j >= boneCount)
                {
                    continue;
                }
                const float d2 = NS::Vector3::DistanceSquared(v.position, spheres[j].center);
                if (d2 > maxD2[j])
                {
                    maxD2[j] = d2;
                }
            }
        }
        for (std::size_t j = 0; j < boneCount; ++j)
        {
            if (hit[j])
            {
                spheres[j].radius = std::sqrt(maxD2[j]);
            }
        }
        return spheres;
    }

    NS::AABB MergeSkinnedBounds(const std::vector<BoneSphere>& spheres,
                                      const NS::Matrix* palette,
                                      std::size_t paletteCount,
                                      const NS::AABB& fallback)
    {
        if (palette == nullptr)
        {
            NS_LOG_ERROR(Graphics, "MergeSkinnedBounds: palette が nullptr");
            return fallback;
        }

        NS::AABB merged{};
        bool any = false;
        const std::size_t count = std::min(spheres.size(), paletteCount);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (spheres[i].radius < 0.0f)
            {
                continue; // 影響頂点なしのボーンは飛ばす
            }

            // 球中心だけ現在ポーズへ動かし半径そのままの箱にする。剛体変換なら半径は変わらない
            const NS::Vector3 c = NS::Vector3::Transform(spheres[i].center, palette[i]);
            const float r = spheres[i].radius;
            const NS::AABB box{c, NS::Vector3{r, r, r}};
            if (!any)
            {
                merged = box;
                any = true;
            }
            else
            {
                NS::AABB::CreateMerged(merged, merged, box);
            }
        }
        if (!any)
        {
            return fallback;
        }
        return merged;
    }

    std::unique_ptr<SkeletalMesh> SkeletalMesh::Create(const SkinnedMeshDesc& desc)
    {
        return std::unique_ptr<SkeletalMesh>(new SkeletalMesh(desc));
    }

    SkeletalMesh::SkeletalMesh(const SkinnedMeshDesc& desc)
    {
        if (Gpu().device == nullptr || Gpu().context == nullptr)
        {
            NS_LOG_ERROR(Graphics, "SkeletalMesh: Renderer の Device / Context が無効");
            return;
        }

        SetVertexLayout(SkinnedInputLayout());

        const bool descValid =
            (desc.vertices != nullptr && desc.vertexCount != 0u && desc.indices != nullptr && desc.indexCount != 0u);
        if (!descValid)
        {
            NS_LOG_ERROR(Graphics,
                         "SkeletalMesh: SkinnedMeshDesc 不正 — IsValid false (vertices={}, vCount={}, indices={}, "
                         "iCount={})",
                         static_cast<const void*>(desc.vertices),
                         desc.vertexCount,
                         static_cast<const void*>(desc.indices),
                         desc.indexCount);
            return;
        }

        if (!BuildGeometry(
                desc.vertices, desc.vertexCount, sizeof(SkinnedVertex), desc.indices, desc.indexCount, false))
        {
            NS_LOG_ERROR(Graphics,
                         "SkeletalMesh: VertexBuffer / IndexBuffer 構築失敗 — IsValid false (v={}, i={})",
                         desc.vertexCount,
                         desc.indexCount);
            return;
        }

        // バインドポーズ実測箱。アニメ非再生時のフォールバック境界に使う
        NS::AABB bounds{};
        DirectX::BoundingBox::CreateFromPoints(bounds,
                                               desc.vertexCount,
                                               static_cast<const DirectX::XMFLOAT3*>(&desc.vertices[0].position),
                                               sizeof(SkinnedVertex));
        SetLocalBounds(bounds);

        if (desc.boneCount > k_MaxBones)
        {
            NS_LOG_ERROR(
                Graphics, "SkeletalMesh: boneCount {} が上限 {} を超過 — 上限に切詰め", desc.boneCount, k_MaxBones);
        }
        m_boneCount = std::min(desc.boneCount, k_MaxBones);

        // 現在ポーズの締まった境界を組む材料。実行時は球とパレットだけで頂点は触らない
        m_boneSpheres = ComputeBoneSpheres(desc.vertices, desc.vertexCount, m_boneCount);
    }

    SkeletalMesh::~SkeletalMesh() = default;

    std::vector<InputElement> SkeletalMesh::SkinnedInputLayout()
    {
        static const std::vector<InputElement> k_Layout = {
            InputElement{
                "POSITION", InputElementFormat::Float3, static_cast<unsigned>(offsetof(SkinnedVertex, position))},
            InputElement{"TEXCOORD", InputElementFormat::Float2, static_cast<unsigned>(offsetof(SkinnedVertex, uv))},
            InputElement{"NORMAL", InputElementFormat::Float3, static_cast<unsigned>(offsetof(SkinnedVertex, normal))},
            InputElement{
                "BLENDINDICES", InputElementFormat::UInt4, static_cast<unsigned>(offsetof(SkinnedVertex, joints))},
            InputElement{
                "BLENDWEIGHT", InputElementFormat::Float4, static_cast<unsigned>(offsetof(SkinnedVertex, weights))},
        };
        return k_Layout;
    }

    std::size_t SkeletalMesh::BoneCount() const noexcept
    {
        return m_boneCount;
    }

    const std::vector<BoneSphere>& SkeletalMesh::BoneSpheres() const noexcept
    {
        return m_boneSpheres;
    }

} // namespace NS::Gfx
