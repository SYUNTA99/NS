#include "Runtime/Graphics/SkeletalMesh.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/GraphicObject.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NS::Graphics
{
    namespace
    {
        bool BuildBuffers(const SkinnedVertex* vertices,
                          std::size_t vertexCount,
                          const std::uint32_t* indices,
                          std::size_t indexCount,
                          std::unique_ptr<Buffer>& outVb,
                          std::unique_ptr<Buffer>& outIb)
        {
            BufferDesc vbDesc = MakeVertexBufferDesc(vertices, vertexCount, sizeof(SkinnedVertex));
            std::unique_ptr<Buffer> vb = Buffer::Create(vbDesc);
            if (!vb->IsValid())
                return false;

            BufferDesc ibDesc = MakeIndexBufferDesc(indices, indexCount, DXGI_FORMAT_R32_UINT);
            std::unique_ptr<Buffer> ib = Buffer::Create(ibDesc);
            if (!ib->IsValid())
                return false;

            outVb = std::move(vb);
            outIb = std::move(ib);
            return true;
        }
    } // namespace

    std::vector<BoneSphere> ComputeBoneSpheres(const SkinnedVertex* vertices,
                                               std::size_t vertexCount,
                                               std::size_t boneCount)
    {
        std::vector<BoneSphere> spheres(boneCount);
        for (BoneSphere& s : spheres)
            s.radius = -1.0f; // 既定は影響なし。頂点が当たったボーンだけ後で半径を入れる
        if (vertices == nullptr || vertexCount == 0u || boneCount == 0u)
            return spheres;

        constexpr float k_Big = std::numeric_limits<float>::max();
        std::vector<NS::Math::Vector3> mn(boneCount, NS::Math::Vector3{k_Big, k_Big, k_Big});
        std::vector<NS::Math::Vector3> mx(boneCount, NS::Math::Vector3{-k_Big, -k_Big, -k_Big});
        std::vector<bool> hit(boneCount, false);

        // 各頂点を weight>0 の全影響ボーンへ算入し、ボーンごとの軸並行範囲を取る
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            const SkinnedVertex& v = vertices[i];
            for (int k = 0; k < 4; ++k)
            {
                if (v.weights[k] <= 0.0f)
                    continue;
                const std::uint32_t j = v.joints[k];
                if (j >= boneCount)
                    continue;
                mn[j] = NS::Math::Vector3::Min(mn[j], v.position);
                mx[j] = NS::Math::Vector3::Max(mx[j], v.position);
                hit[j] = true;
            }
        }
        for (std::size_t j = 0; j < boneCount; ++j)
            if (hit[j])
                spheres[j].center = (mn[j] + mx[j]) * 0.5f;

        // 中心が決まってから最遠影響頂点までの距離を半径にする
        std::vector<float> maxD2(boneCount, 0.0f);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            const SkinnedVertex& v = vertices[i];
            for (int k = 0; k < 4; ++k)
            {
                if (v.weights[k] <= 0.0f)
                    continue;
                const std::uint32_t j = v.joints[k];
                if (j >= boneCount)
                    continue;
                const float d2 = NS::Math::Vector3::DistanceSquared(v.position, spheres[j].center);
                if (d2 > maxD2[j])
                    maxD2[j] = d2;
            }
        }
        for (std::size_t j = 0; j < boneCount; ++j)
            if (hit[j])
                spheres[j].radius = std::sqrt(maxD2[j]);
        return spheres;
    }

    NS::Math::AABB MergeSkinnedBounds(const std::vector<BoneSphere>& spheres,
                                      const NS::Math::Matrix* palette,
                                      std::size_t paletteCount,
                                      const NS::Math::AABB& fallback)
    {
        if (palette == nullptr)
            return fallback;

        NS::Math::AABB merged{};
        bool any = false;
        const std::size_t count = std::min(spheres.size(), paletteCount);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (spheres[i].radius < 0.0f)
                continue; // 影響頂点なしのボーンは飛ばす
            // 球中心だけ現在ポーズへ動かし半径そのままの箱にする。剛体変換なら半径は変わらない
            const NS::Math::Vector3 c = NS::Math::Vector3::Transform(spheres[i].center, palette[i]);
            const float r = spheres[i].radius;
            const NS::Math::AABB box{c, NS::Math::Vector3{r, r, r}};
            if (!any)
            {
                merged = box;
                any = true;
            }
            else
            {
                NS::Math::AABB::CreateMerged(merged, merged, box);
            }
        }
        if (!any)
            return fallback;
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

        std::unique_ptr<Buffer> vb;
        std::unique_ptr<Buffer> ib;
        if (!BuildBuffers(desc.vertices, desc.vertexCount, desc.indices, desc.indexCount, vb, ib))
        {
            NS_LOG_ERROR(Graphics,
                         "SkeletalMesh: VertexBuffer / IndexBuffer 構築失敗 — IsValid false (v={}, i={})",
                         desc.vertexCount,
                         desc.indexCount);
            return;
        }
        SetGeometry(std::move(vb), std::move(ib), desc.vertexCount, desc.indexCount, false);

        // バインドポーズ実測箱。アニメ非再生時のフォールバック境界に使う
        NS::Math::AABB bounds{};
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

} // namespace NS::Graphics
