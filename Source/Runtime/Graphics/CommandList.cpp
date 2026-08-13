#include "Runtime/Graphics/CommandList.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/Pipeline.h"
#include "Runtime/Graphics/Shader.h"
#include "Runtime/Graphics/Texture.h"
#include "Runtime/Graphics/TextureArray.h"

namespace NS::Graphics
{
    namespace
    {
        D3D11_PRIMITIVE_TOPOLOGY ToD3d(Topology topology) noexcept
        {
            return (topology == Topology::LineList) ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST
                                                    : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }

        bool MapAndCopy(ID3D11DeviceContext* context,
                        ID3D11Buffer* buffer,
                        const void* data,
                        std::size_t bytes) noexcept
        {
            if (context == nullptr || buffer == nullptr || data == nullptr || bytes == 0u)
            {
                NS_LOG_ERROR(Graphics,
                             "CommandList::Update: 引数不正 (context={}, buffer={}, data={}, bytes={})",
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
                NS_LOG_ERROR(Graphics, "Buffer::Map 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
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

    void CommandList::SetPipeline(const Pipeline& pipeline) noexcept
    {
        if (m_context == nullptr || !pipeline.IsValid())
        {
            return;
        }
        m_context->RSSetState(pipeline.RasterizerState());
        m_context->OMSetBlendState(pipeline.BlendState(), nullptr, 0xFFFFFFFFu);
        m_context->OMSetDepthStencilState(pipeline.DepthStencilState(), 0u);
    }

    // InputLayout は NS にラッパ型が無いため Mesh が所有する生 ID3D11InputLayout* をそのまま受ける
    void CommandList::SetInputLayout(ID3D11InputLayout* layout) noexcept
    {
        if (m_context == nullptr || layout == nullptr)
        {
            return;
        }
        m_context->IASetInputLayout(layout);
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

    void CommandList::SetTopology(Topology topology) noexcept
    {
        if (m_context == nullptr)
        {
            return;
        }
        m_context->IASetPrimitiveTopology(ToD3d(topology));
    }

    void CommandList::UpdateSubresource(const Buffer& buffer, const void* data, std::size_t bytes) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid() || data == nullptr || bytes == 0u)
        {
            return;
        }
        if (bytes > buffer.ByteSize())
        {
            NS_LOG_ERROR(Graphics, "CommandList::Update: buffer サイズ超過 (req={}, max={})", bytes, buffer.ByteSize());
            return;
        }
        ID3D11Buffer* native = buffer.Native();
        D3D11_BUFFER_DESC desc{};
        native->GetDesc(&desc);
        switch (desc.Usage)
        {
        case D3D11_USAGE_DYNAMIC:
            MapAndCopy(m_context, native, data, bytes);
            break;
        case D3D11_USAGE_DEFAULT:
        {
            // 先頭 bytes だけ更新する。box=nullptr は全域前提でソースを範囲外読みするため box を渡す
            D3D11_BOX box{};
            box.right = static_cast<UINT>(bytes);
            box.bottom = 1u;
            box.back = 1u;
            m_context->UpdateSubresource(native, 0u, &box, data, static_cast<UINT>(bytes), 0u);
            break;
        }
        default:
            NS_LOG_ERROR(Graphics,
                         "CommandList::Update: IMMUTABLE/STAGING buffer は更新不可 (usage={})",
                         static_cast<int>(desc.Usage));
            break;
        }
    }

    void CommandList::UpdateSubresource(const Texture& texture, const void* data, unsigned rowPitch) noexcept
    {
        if (m_context == nullptr || !texture.IsValid() || data == nullptr || rowPitch == 0u)
        {
            return;
        }
        ID3D11Texture2D* native = texture.Native();
        if (native == nullptr)
        {
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        native->GetDesc(&desc);
        switch (desc.Usage)
        {
        case D3D11_USAGE_DEFAULT:
            m_context->UpdateSubresource(native, 0u, nullptr, data, rowPitch, 0u);
            break;
        case D3D11_USAGE_DYNAMIC:
        {
            // Map が返す RowPitch はドライバ都合で rowPitch と異なり得るので行ごとにコピーする
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m_context->Map(native, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped)))
            {
                return;
            }
            const auto* src = static_cast<const std::byte*>(data);
            auto* dst = static_cast<std::byte*>(mapped.pData);
            for (UINT y = 0u; y < desc.Height; ++y)
            {
                std::memcpy(dst + static_cast<std::size_t>(y) * mapped.RowPitch,
                            src + static_cast<std::size_t>(y) * rowPitch,
                            rowPitch);
            }
            m_context->Unmap(native, 0u);
            break;
        }
        default:
            NS_LOG_ERROR(Graphics,
                         "CommandList::Update: IMMUTABLE/STAGING texture は更新不可 (usage={})",
                         static_cast<int>(desc.Usage));
            break;
        }
    }

    // 頂点シェーダー
    // 実型は生成時のステージで決まる。Type を検証してから static_cast で下ろす
    void CommandList::VSSetShader(const Shader& shader) noexcept
    {
        if (m_context == nullptr)
        {
            return;
        }
        if (shader.Type() != ShaderType::Vertex)
        {
            NS_LOG_ERROR(Graphics,
                         "CommandList::VSSetShader: 頂点以外の Shader を渡した (type={})",
                         static_cast<int>(shader.Type()));
            return;
        }
        ID3D11DeviceChild* raw = shader.Native();
        if (raw == nullptr)
        {
            return;
        }
        m_context->VSSetShader(static_cast<ID3D11VertexShader*>(raw), nullptr, 0u);
    }

    void CommandList::VSSetShaderResource(const Texture& texture, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = texture.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->VSSetShaderResources(slot, 1u, srvs);
    }

    void CommandList::VSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = textureArray.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->VSSetShaderResources(slot, 1u, srvs);
    }

    // サンプラーは NS にラッパ型が無いため生の ID3D11SamplerState* をそのまま受ける
    void CommandList::VSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept
    {
        if (m_context == nullptr || sampler == nullptr)
        {
            return;
        }
        ID3D11SamplerState* samplers[1] = {sampler};
        m_context->VSSetSamplers(slot, 1u, samplers);
    }

    void CommandList::VSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        ID3D11Buffer* buffers[1] = {buffer.Native()};
        m_context->VSSetConstantBuffers(slot, 1u, buffers);
    }

    // ピクセルシェーダー
    void CommandList::PSSetShader(const Shader& shader) noexcept
    {
        if (m_context == nullptr)
        {
            return;
        }
        if (shader.Type() != ShaderType::Pixel)
        {
            NS_LOG_ERROR(Graphics,
                         "CommandList::PSSetShader: ピクセル以外の Shader を渡した (type={})",
                         static_cast<int>(shader.Type()));
            return;
        }
        ID3D11DeviceChild* raw = shader.Native();
        if (raw == nullptr)
        {
            return;
        }
        m_context->PSSetShader(static_cast<ID3D11PixelShader*>(raw), nullptr, 0u);
    }

    void CommandList::PSSetShaderResource(const Texture& texture, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = texture.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->PSSetShaderResources(slot, 1u, srvs);
    }

    void CommandList::PSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = textureArray.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->PSSetShaderResources(slot, 1u, srvs);
    }

    void CommandList::PSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept
    {
        if (m_context == nullptr || sampler == nullptr)
        {
            return;
        }
        ID3D11SamplerState* samplers[1] = {sampler};
        m_context->PSSetSamplers(slot, 1u, samplers);
    }

    void CommandList::PSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        ID3D11Buffer* buffers[1] = {buffer.Native()};
        m_context->PSSetConstantBuffers(slot, 1u, buffers);
    }

    // ジオメトリシェーダー
    void CommandList::GSSetShader(const Shader& shader) noexcept
    {
        if (m_context == nullptr)
        {
            return;
        }
        if (shader.Type() != ShaderType::Geometry)
        {
            NS_LOG_ERROR(Graphics,
                         "CommandList::GSSetShader: ジオメトリ以外の Shader を渡した (type={})",
                         static_cast<int>(shader.Type()));
            return;
        }
        ID3D11DeviceChild* raw = shader.Native();
        if (raw == nullptr)
        {
            return;
        }
        m_context->GSSetShader(static_cast<ID3D11GeometryShader*>(raw), nullptr, 0u);
    }

    void CommandList::GSSetShaderResource(const Texture& texture, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = texture.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->GSSetShaderResources(slot, 1u, srvs);
    }

    void CommandList::GSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = textureArray.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->GSSetShaderResources(slot, 1u, srvs);
    }

    void CommandList::GSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept
    {
        if (m_context == nullptr || sampler == nullptr)
        {
            return;
        }
        ID3D11SamplerState* samplers[1] = {sampler};
        m_context->GSSetSamplers(slot, 1u, samplers);
    }

    void CommandList::GSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        ID3D11Buffer* buffers[1] = {buffer.Native()};
        m_context->GSSetConstantBuffers(slot, 1u, buffers);
    }

    // コンピュートシェーダー
    void CommandList::CSSetShader(const Shader& shader) noexcept
    {
        if (m_context == nullptr)
        {
            return;
        }
        if (shader.Type() != ShaderType::Compute)
        {
            NS_LOG_ERROR(Graphics,
                         "CommandList::CSSetShader: コンピュート以外の Shader を渡した (type={})",
                         static_cast<int>(shader.Type()));
            return;
        }
        ID3D11DeviceChild* raw = shader.Native();
        if (raw == nullptr)
        {
            return;
        }
        m_context->CSSetShader(static_cast<ID3D11ComputeShader*>(raw), nullptr, 0u);
    }

    void CommandList::CSSetShaderResource(const Texture& texture, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = texture.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->CSSetShaderResources(slot, 1u, srvs);
    }

    void CommandList::CSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept
    {
        ID3D11ShaderResourceView* srv = textureArray.Srv();
        if (m_context == nullptr || srv == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->CSSetShaderResources(slot, 1u, srvs);
    }

    void CommandList::CSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept
    {
        if (m_context == nullptr || sampler == nullptr)
        {
            return;
        }
        ID3D11SamplerState* samplers[1] = {sampler};
        m_context->CSSetSamplers(slot, 1u, samplers);
    }

    void CommandList::CSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept
    {
        if (m_context == nullptr || !buffer.IsValid())
        {
            return;
        }
        ID3D11Buffer* buffers[1] = {buffer.Native()};
        m_context->CSSetConstantBuffers(slot, 1u, buffers);
    }

    void CommandList::DrawIndexed(unsigned indexCount) noexcept
    {
        if (m_context != nullptr)
        {
            m_context->DrawIndexed(indexCount, 0u, 0);
        }
    }

    void CommandList::Draw(unsigned vertexCount) noexcept
    {
        if (m_context != nullptr)
        {
            m_context->Draw(vertexCount, 0u);
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
