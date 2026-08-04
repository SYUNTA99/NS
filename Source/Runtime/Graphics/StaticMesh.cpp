#include "Runtime/Graphics/StaticMesh.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/MeshPrimitives.h"
#include "Runtime/Graphics/Renderer.h"

namespace NS::Graphics
{
    namespace
    {
        // モデルの読み込みに失敗した場合に表示する、デバッグ用の代替モデル
        constexpr float k_FallbackCubeHalfExtent = 0.5f;

        bool BuildBuffers(const StaticVertex* vertices,
                          std::size_t vertexCount,
                          const std::uint32_t* indices,
                          std::size_t indexCount,
                          std::unique_ptr<Buffer>& outVb,
                          std::unique_ptr<Buffer>& outIb)
        {
            BufferDesc vbDesc = MakeVertexBufferDesc(vertices, vertexCount, sizeof(StaticVertex));
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

        // 頂点位置データから、モデルのAABBを算出する
        NS::Math::AABB ComputeLocalBounds(const StaticVertex* vertices, std::size_t count)
        {
            NS::Math::AABB box{};
            if (vertices != nullptr && count > 0u)
                DirectX::BoundingBox::CreateFromPoints(
                    box, count, static_cast<const DirectX::XMFLOAT3*>(&vertices[0].position), sizeof(StaticVertex));
            return box;
        }
    } // namespace

    std::unique_ptr<StaticMesh> StaticMesh::Create(const MeshDesc& desc)
    {
        return std::unique_ptr<StaticMesh>(new StaticMesh(desc));
    }

    StaticMesh::StaticMesh(const MeshDesc& desc)
    {
        if (Gpu().device == nullptr || Gpu().context == nullptr)
        {
            NS_LOG_ERROR(Graphics, "StaticMesh: Renderer の Device / Context が無効");
            return;
        }

        SetVertexLayout(StandardInputLayout());

        std::unique_ptr<Buffer> vb;
        std::unique_ptr<Buffer> ib;

        const bool descValid =
            (desc.vertices != nullptr && desc.vertexCount != 0u && desc.indices != nullptr && desc.indexCount != 0u);
        if (descValid && BuildBuffers(desc.vertices, desc.vertexCount, desc.indices, desc.indexCount, vb, ib))
        {
            SetGeometry(std::move(vb), std::move(ib), desc.vertexCount, desc.indexCount, false);
            // ローダーが構築時に境界を求めていればそれを使い、無ければ頂点から算出する
            if (desc.precomputedBounds != nullptr)
                SetLocalBounds(*desc.precomputedBounds);
            else
                SetLocalBounds(ComputeLocalBounds(desc.vertices, desc.vertexCount));
            return;
        }

        if (!descValid)
            NS_LOG_ERROR(
                Graphics,
                "StaticMesh: MeshDesc 不正 — fallback Cube に切替 (vertices={}, vCount={}, indices={}, iCount={})",
                static_cast<const void*>(desc.vertices),
                desc.vertexCount,
                static_cast<const void*>(desc.indices),
                desc.indexCount);
        else
            NS_LOG_ERROR(Graphics,
                         "StaticMesh: VertexBuffer / IndexBuffer 構築失敗 — fallback Cube に切替 (v={}, i={})",
                         desc.vertexCount,
                         desc.indexCount);

        const MeshGeometry geom =
            MakeCube(NS::Math::Vector3{k_FallbackCubeHalfExtent, k_FallbackCubeHalfExtent, k_FallbackCubeHalfExtent});
        if (BuildBuffers(geom.vertices.data(), geom.vertices.size(), geom.indices.data(), geom.indices.size(), vb, ib))
        {
            SetGeometry(std::move(vb), std::move(ib), geom.vertices.size(), geom.indices.size(), true);
            SetLocalBounds(ComputeLocalBounds(geom.vertices.data(), geom.vertices.size()));
        }
        else
            NS_LOG_ERROR(Graphics, "StaticMesh: fallback Cube 構築失敗");
    }

    std::vector<InputElement> StaticMesh::StandardInputLayout()
    {
        static const std::vector<InputElement> k_Layout = {
            InputElement{
                "POSITION", InputElementFormat::Float3, static_cast<unsigned>(offsetof(StaticVertex, position))},
            InputElement{"TEXCOORD", InputElementFormat::Float2, static_cast<unsigned>(offsetof(StaticVertex, uv))},
            InputElement{"NORMAL", InputElementFormat::Float3, static_cast<unsigned>(offsetof(StaticVertex, normal))},
        };
        return k_Layout;
    }

} // namespace NS::Graphics
