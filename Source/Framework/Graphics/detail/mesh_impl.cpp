#include "Framework/Graphics/Mesh.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    using detail::ComPtr;

    struct Mesh::Impl
    {
        std::unique_ptr<Buffer> vb;
        std::unique_ptr<Buffer> ib;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11InputLayout> inputLayout;
        std::vector<InputElement> layoutElements;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        bool valid = false;
        bool usingFallback = false;
    };

    namespace
    {
        [[nodiscard]] DXGI_FORMAT ToDxgiFormat(InputElementFormat fmt) noexcept
        {
            switch (fmt)
            {
            case InputElementFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case InputElementFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case InputElementFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case InputElementFormat::UInt32:
                return DXGI_FORMAT_R32_UINT;
            case InputElementFormat::UInt4:
                return DXGI_FORMAT_R32G32B32A32_UINT;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

        bool CreateInputLayoutFromDesc(ID3D11Device* device,
                                       const void* vsBytecode,
                                       std::size_t vsBytecodeSize,
                                       const std::vector<InputElement>& elements,
                                       ComPtr<ID3D11InputLayout>& outLayout) noexcept
        {
            if (device == nullptr || vsBytecode == nullptr || vsBytecodeSize == 0u || elements.empty())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Mesh::CreateInputLayout: 引数不正 (device={}, vsBytecode={}, vsSize={}, elements={})",
                             static_cast<const void*>(device),
                             vsBytecode,
                             vsBytecodeSize,
                             elements.size());
                return false;
            }
            std::vector<D3D11_INPUT_ELEMENT_DESC> descs;
            descs.reserve(elements.size());
            for (const auto& e : elements)
            {
                D3D11_INPUT_ELEMENT_DESC d{};
                d.SemanticName = e.semanticName.c_str();
                d.SemanticIndex = 0u;
                d.Format = ToDxgiFormat(e.format);
                d.InputSlot = 0u;
                d.AlignedByteOffset = e.byteOffset;
                d.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                d.InstanceDataStepRate = 0u;
                descs.push_back(d);
            }
            const HRESULT hr = device->CreateInputLayout(
                descs.data(), static_cast<UINT>(descs.size()), vsBytecode, vsBytecodeSize, outLayout.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Mesh::CreateInputLayout: CreateInputLayout 失敗 (hr=0x{:X}, elements={})",
                             static_cast<unsigned>(hr),
                             elements.size());
                return false;
            }
            return true;
        }
    } // namespace

    Mesh::Mesh() : m_pImpl(std::make_unique<Impl>()) {}
    Mesh::~Mesh() = default;

    void Mesh::SetGeometry(Renderer& renderer,
                           std::unique_ptr<Buffer> vertexBuffer,
                           std::unique_ptr<Buffer> indexBuffer,
                           std::size_t vertexCount,
                           std::size_t indexCount,
                           bool usingFallback) noexcept
    {
        if (!m_pImpl)
            return;
        m_pImpl->device = detail::GetDevice(renderer);
        m_pImpl->vb = std::move(vertexBuffer);
        m_pImpl->ib = std::move(indexBuffer);
        m_pImpl->vertexCount = vertexCount;
        m_pImpl->indexCount = indexCount;
        m_pImpl->usingFallback = usingFallback;
        m_pImpl->valid = (m_pImpl->vb != nullptr && m_pImpl->ib != nullptr && m_pImpl->device != nullptr);
    }

    void Mesh::SetVertexLayout(std::vector<InputElement> elements) noexcept
    {
        if (m_pImpl)
            m_pImpl->layoutElements = std::move(elements);
    }

    void Mesh::CreateInputLayout(const Shader& vertexShader) noexcept
    {
        if (!m_pImpl || m_pImpl->inputLayout)
            return;
        if (m_pImpl->device == nullptr || m_pImpl->layoutElements.empty())
            return;

        const std::span<const std::byte> bytecode = detail::GetVertexShaderBytecode(vertexShader);
        if (bytecode.empty())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Mesh::CreateInputLayout: VS バイトコードが空");
            return;
        }

        ComPtr<ID3D11InputLayout> layout;
        if (CreateInputLayoutFromDesc(
                m_pImpl->device.Get(), bytecode.data(), bytecode.size(), m_pImpl->layoutElements, layout))
        {
            m_pImpl->inputLayout = std::move(layout);
        }
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

    void Mesh::Draw(Renderer& renderer) noexcept
    {
        if (!IsValid())
            return;
        auto* context = detail::GetContext(renderer);
        if (m_pImpl->inputLayout && context != nullptr)
            context->IASetInputLayout(m_pImpl->inputLayout.Get());
        renderer.BindVertexBuffer(*m_pImpl->vb, 0);
        renderer.BindIndexBuffer(*m_pImpl->ib);
        if (context != nullptr)
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        renderer.DrawIndexed(static_cast<unsigned>(m_pImpl->indexCount));
    }

    namespace detail
    {
        ID3D11Buffer* GetVertexBuffer(Mesh& mesh) noexcept
        {
            return (mesh.m_pImpl && mesh.m_pImpl->vb) ? mesh.m_pImpl->vb->Native() : nullptr;
        }
        ID3D11Buffer* GetIndexBuffer(Mesh& mesh) noexcept
        {
            return (mesh.m_pImpl && mesh.m_pImpl->ib) ? mesh.m_pImpl->ib->Native() : nullptr;
        }
        ID3D11InputLayout* GetInputLayout(Mesh& mesh) noexcept
        {
            return mesh.m_pImpl ? mesh.m_pImpl->inputLayout.Get() : nullptr;
        }
    } // namespace detail

} // namespace NS::Graphics
