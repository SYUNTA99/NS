#include "Framework/Graphics/Mesh.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include <cstddef>
#include <utility>

namespace NS::Graphics
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
        bool usingFallback = false;
    };

    Mesh::Mesh() : m_pImpl(std::make_unique<Impl>()) {}
    Mesh::~Mesh() = default;

    void Mesh::SetGeometry(Renderer& renderer,
                           std::unique_ptr<VertexBuffer> vertexBuffer,
                           std::unique_ptr<IndexBuffer> indexBuffer,
                           std::size_t vertexCount,
                           std::size_t indexCount,
                           bool usingFallback) noexcept
    {
        if (!m_pImpl)
            return;
        m_pImpl->context = detail::GetContext(renderer);
        m_pImpl->vb = std::move(vertexBuffer);
        m_pImpl->ib = std::move(indexBuffer);
        m_pImpl->vertexCount = vertexCount;
        m_pImpl->indexCount = indexCount;
        m_pImpl->usingFallback = usingFallback;
        m_pImpl->valid = (m_pImpl->vb != nullptr && m_pImpl->ib != nullptr && m_pImpl->context != nullptr);
    }

    bool Mesh::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }
    bool Mesh::IsUsingFallback() const noexcept
    {
        return m_pImpl && m_pImpl->usingFallback;
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
            return;
        m_pImpl->vb->Bind(0);
        m_pImpl->ib->Bind();
        m_pImpl->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_pImpl->context->DrawIndexed(static_cast<UINT>(m_pImpl->indexCount), 0u, 0);
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

} // namespace NS::Graphics
