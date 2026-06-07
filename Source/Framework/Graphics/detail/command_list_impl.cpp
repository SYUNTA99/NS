#include "Framework/Graphics/CommandList.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/ShaderStage.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/TextureArray.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cstring>

namespace NS::Graphics
{
    namespace
    {
        bool MapAndCopy(ID3D11DeviceContext* context,
                        ID3D11Buffer* buffer,
                        const void* data,
                        std::size_t bytes) noexcept
        {
            if (context == nullptr || buffer == nullptr || data == nullptr || bytes == 0u)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "CommandList::UpdateBuffer: 引数不正 (context={}, buffer={}, data={}, bytes={})",
                             static_cast<const void*>(context),
                             static_cast<const void*>(buffer),
                             data,
                             bytes);
                return false;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT hr = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Buffer::Map 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            std::memcpy(mapped.pData, data, bytes);
            context->Unmap(buffer, 0);
            return true;
        }
    } // namespace

    CommandList::CommandList(ID3D11DeviceContext* context) noexcept : m_context(context) {}

    void CommandList::SetRenderTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv) noexcept
    {
        if (m_context == nullptr)
        {
            return;
        }
        ID3D11RenderTargetView* rtvs[1] = {rtv};
        m_context->OMSetRenderTargets(1u, rtvs, dsv);
    }

    void CommandList::ClearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a) noexcept
    {
        if (m_context == nullptr || rtv == nullptr)
        {
            return;
        }
        const float color[4] = {r, g, b, a};
        m_context->ClearRenderTargetView(rtv, color);
    }

    void CommandList::ClearDepth(ID3D11DepthStencilView* dsv, float depth) noexcept
    {
        if (m_context == nullptr || dsv == nullptr)
        {
            return;
        }
        m_context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, 0);
    }

    void CommandList::SetViewport(float width, float height) noexcept
    {
        if (m_context == nullptr)
        {
            return;
        }
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = width;
        vp.Height = height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1u, &vp);
    }

    void CommandList::SetShader(const Shader& shader) noexcept
    {
        ID3D11DeviceChild* raw = shader.Native();
        if (m_context == nullptr || raw == nullptr)
        {
            return;
        }
        // 生成時のステージで実型は保証済みなので static_cast 下方変換は well-defined
        switch (shader.Type())
        {
        case ShaderType::Vertex:
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(raw), nullptr, 0u);
            break;
        case ShaderType::Pixel:
            m_context->PSSetShader(static_cast<ID3D11PixelShader*>(raw), nullptr, 0u);
            break;
        case ShaderType::Geometry:
            m_context->GSSetShader(static_cast<ID3D11GeometryShader*>(raw), nullptr, 0u);
            break;
        case ShaderType::Hull:
            m_context->HSSetShader(static_cast<ID3D11HullShader*>(raw), nullptr, 0u);
            break;
        case ShaderType::Domain:
            m_context->DSSetShader(static_cast<ID3D11DomainShader*>(raw), nullptr, 0u);
            break;
        case ShaderType::Compute:
            m_context->CSSetShader(static_cast<ID3D11ComputeShader*>(raw), nullptr, 0u);
            break;
        }
    }

    void CommandList::SetTexture(const Texture& texture, unsigned slot, ShaderStage stages) noexcept
    {
        ID3D11ShaderResourceView* srv = texture.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        if (HasStage(stages, ShaderStage::Vertex))
        {
            m_context->VSSetShaderResources(slot, 1u, srvs);
        }
        if (HasStage(stages, ShaderStage::Pixel))
        {
            m_context->PSSetShaderResources(slot, 1u, srvs);
        }
    }

    void CommandList::SetTextureArray(const TextureArray& textureArray, unsigned slot, ShaderStage stages) noexcept
    {
        ID3D11ShaderResourceView* srv = textureArray.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        if (HasStage(stages, ShaderStage::Vertex))
        {
            m_context->VSSetShaderResources(slot, 1u, srvs);
        }
        if (HasStage(stages, ShaderStage::Pixel))
        {
            m_context->PSSetShaderResources(slot, 1u, srvs);
        }
    }

    void CommandList::SetVertexBuffer(const Buffer& buffer, unsigned slot) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        ID3D11Buffer* buffers[1] = {buffer.Native()};
        const UINT stride = static_cast<UINT>(buffer.Stride());
        const UINT offset = 0u;
        m_context->IASetVertexBuffers(slot, 1u, buffers, &stride, &offset);
    }

    void CommandList::SetIndexBuffer(const Buffer& buffer) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        m_context->IASetIndexBuffer(buffer.Native(), buffer.Format(), 0u);
    }

    void CommandList::SetConstantBuffer(const Buffer& buffer, unsigned slot, ShaderStage stages) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        ID3D11Buffer* buffers[1] = {buffer.Native()};
        if (HasStage(stages, ShaderStage::Vertex))
        {
            m_context->VSSetConstantBuffers(slot, 1u, buffers);
        }
        if (HasStage(stages, ShaderStage::Pixel))
        {
            m_context->PSSetConstantBuffers(slot, 1u, buffers);
        }
    }

    void CommandList::UpdateBuffer(const Buffer& buffer, const void* data, std::size_t bytes) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        if (!buffer.IsDynamic())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Static Buffer への Update は無効 (Dynamic で再構築するか別バッファを使う)");
            return;
        }
        if (bytes > buffer.ByteSize())
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "Buffer::Update のサイズ超過 (req={}, max={})", bytes, buffer.ByteSize());
            return;
        }
        MapAndCopy(m_context, buffer.Native(), data, bytes);
    }

    void CommandList::DrawIndexed(unsigned indexCount) noexcept
    {
        if (m_context != nullptr)
        {
            m_context->DrawIndexed(indexCount, 0u, 0);
        }
    }

    ID3D11DeviceContext* CommandList::Native() const noexcept
    {
        return m_context;
    }

    ID3D11DeviceContext* CommandList::operator->() const noexcept
    {
        return m_context;
    }

} // namespace NS::Graphics
