#include "ns/graphics/mesh.h"

#include "ns/graphics/buffer.h"
#include "ns/graphics/detail/d3d_context.h"
#include "ns/graphics/renderer.h"

#include "ns/core/log_categories.h"
#include "ns/core/logger.h"

#include <cstddef>
#include <utility>

namespace ns::graphics
{
    using detail::ComPtr;

    struct Mesh::Impl
    {
        std::unique_ptr<VertexBuffer> vb;
        std::unique_ptr<IndexBuffer> ib;
        ComPtr<ID3D11DeviceContext> context;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        bool valid = false;
    };

    Mesh::Mesh(Renderer& renderer, const MeshDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        auto* device = detail::GetDevice(renderer);
        auto* context = detail::GetContext(renderer);
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(::ns::core::LogCat::Graphics, "Mesh: Renderer の Device / Context が無効");
            return;
        }
        if (desc.vertices == nullptr || desc.vertexCount == 0u || desc.indices == nullptr || desc.indexCount == 0u)
        {
            NS_LOG_ERROR(::ns::core::LogCat::Graphics,
                         "Mesh: MeshDesc 不正 (vertices={}, vCount={}, indices={}, iCount={})",
                         static_cast<const void*>(desc.vertices),
                         desc.vertexCount,
                         static_cast<const void*>(desc.indices),
                         desc.indexCount);
            return;
        }

        VertexBufferDesc vbd{};
        vbd.initialData = desc.vertices;
        vbd.vertexCount = desc.vertexCount;
        vbd.stride = sizeof(MeshVertex);
        vbd.usage = BufferUsage::Static;
        auto vb = std::make_unique<VertexBuffer>(renderer, vbd);
        if (!vb->IsValid())
        {
            NS_LOG_ERROR(::ns::core::LogCat::Graphics, "Mesh: VertexBuffer 構築失敗 (count={})", desc.vertexCount);
            return;
        }

        IndexBufferDesc ibd{};
        ibd.initialData = desc.indices;
        ibd.indexCount = desc.indexCount;
        ibd.format = IndexFormat::UInt16;
        ibd.usage = BufferUsage::Static;
        auto ib = std::make_unique<IndexBuffer>(renderer, ibd);
        if (!ib->IsValid())
        {
            NS_LOG_ERROR(::ns::core::LogCat::Graphics, "Mesh: IndexBuffer 構築失敗 (count={})", desc.indexCount);
            return;
        }

        m_pImpl->vb = std::move(vb);
        m_pImpl->ib = std::move(ib);
        m_pImpl->context = context;
        m_pImpl->vertexCount = desc.vertexCount;
        m_pImpl->indexCount = desc.indexCount;
        m_pImpl->valid = true;
    }

    Mesh::~Mesh() = default;

    bool Mesh::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }
    std::size_t Mesh::VertexCount() const noexcept
    {
        return m_pImpl ? m_pImpl->vertexCount : 0u;
    }
    std::size_t Mesh::IndexCount() const noexcept
    {
        return m_pImpl ? m_pImpl->indexCount : 0u;
    }

    void Mesh::Draw() noexcept
    {
        if (!IsValid())
        {
            return;
        }
        m_pImpl->vb->Bind(0);
        m_pImpl->ib->Bind();
        m_pImpl->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_pImpl->context->DrawIndexed(static_cast<UINT>(m_pImpl->indexCount), 0u, 0);
    }

    std::vector<InputElement> Mesh::StandardInputLayout()
    {
        static const std::vector<InputElement> kLayout = {
            InputElement{"POSITION", InputElementFormat::Float3, static_cast<unsigned>(offsetof(MeshVertex, position))},
            InputElement{"TEXCOORD", InputElementFormat::Float2, static_cast<unsigned>(offsetof(MeshVertex, uv))},
            InputElement{"NORMAL", InputElementFormat::Float3, static_cast<unsigned>(offsetof(MeshVertex, normal))},
        };
        return kLayout;
    }

    namespace detail
    {
        ID3D11Buffer* GetVertexBuffer(Mesh& mesh) noexcept
        {
            return (mesh.m_pImpl && mesh.m_pImpl->vb) ? GetNative(*mesh.m_pImpl->vb) : nullptr;
        }
        ID3D11Buffer* GetIndexBuffer(Mesh& mesh) noexcept
        {
            return (mesh.m_pImpl && mesh.m_pImpl->ib) ? GetNative(*mesh.m_pImpl->ib) : nullptr;
        }
    } // namespace detail

} // namespace ns::graphics
