#include "Framework/Graphics/StaticMesh.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace NS::Graphics
{
    namespace
    {
        // fallback Cube は player と大きさを揃えた 1m 立方、 default Cube として描画
        constexpr float kFallbackCubeHalfExtent = 0.5f;

        // 生の頂点 / index から VB + IB を構築。 両方 valid なら true を返し out に move する
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
    } // namespace

    std::unique_ptr<StaticMesh> StaticMesh::Create(const MeshDesc& desc)
    {
        return std::unique_ptr<StaticMesh>(new StaticMesh(desc));
    }

    StaticMesh::StaticMesh(const MeshDesc& desc)
    {
        if (Gpu().device == nullptr || Gpu().context == nullptr)
        {
            // device 自体が無いと fallback Cube すら作れない致命状態。geometry 未設定のまま IsValid は false
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "StaticMesh: Renderer の Device / Context が無効");
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
            return;
        }

        if (!descValid)
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics,
                "StaticMesh: MeshDesc 不正 — fallback Cube に切替 (vertices={}, vCount={}, indices={}, iCount={})",
                static_cast<const void*>(desc.vertices),
                desc.vertexCount,
                static_cast<const void*>(desc.indices),
                desc.indexCount);
        else
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "StaticMesh: VertexBuffer / IndexBuffer 構築失敗 — fallback Cube に切替 (v={}, i={})",
                         desc.vertexCount,
                         desc.indexCount);

        const MeshGeometry geom =
            MakeCube(NS::Math::Vector3{kFallbackCubeHalfExtent, kFallbackCubeHalfExtent, kFallbackCubeHalfExtent});
        if (BuildBuffers(geom.vertices.data(), geom.vertices.size(), geom.indices.data(), geom.indices.size(), vb, ib))
            SetGeometry(std::move(vb), std::move(ib), geom.vertices.size(), geom.indices.size(), true);
        else
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "StaticMesh: fallback Cube 構築失敗");
    }

    std::vector<InputElement> StaticMesh::StandardInputLayout()
    {
        static const std::vector<InputElement> kLayout = {
            InputElement{
                "POSITION", InputElementFormat::Float3, static_cast<unsigned>(offsetof(StaticVertex, position))},
            InputElement{"TEXCOORD", InputElementFormat::Float2, static_cast<unsigned>(offsetof(StaticVertex, uv))},
            InputElement{"NORMAL", InputElementFormat::Float3, static_cast<unsigned>(offsetof(StaticVertex, normal))},
        };
        return kLayout;
    }

} // namespace NS::Graphics
