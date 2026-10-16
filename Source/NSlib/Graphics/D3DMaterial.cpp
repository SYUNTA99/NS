#include "NSlib/Graphics/D3DMaterial.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/D3DBuffer.h"
#include "NSlib/Graphics/D3DCommandList.h"
#include "NSlib/Graphics/D3DCommonStates.h"
#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/D3DMesh.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Graphics/D3DShader.h"
#include "NSlib/Graphics/D3DTexture.h"

#include <map>

namespace NS
{
    namespace Gfx
    {
        std::unique_ptr<D3DMaterial> D3DMaterial::Create(const D3DMaterialDesc& desc)
        {
            return std::unique_ptr<D3DMaterial>(new D3DMaterial(desc));
        }

        D3DMaterial::D3DMaterial(const D3DMaterialDesc& desc)
        {
            // 構築に失敗した場合でも後から参照できるように、設定値を先に保存しておく
            m_blend = desc.blend;
            m_renderPriority = desc.renderPriority;

            // Renderer の無い場 (GPU の無い試し) では、ERROR を出さずに空のマテリアルを返す
            // ゲームでは Renderer を先に作るので、ここで返らない
            if (Gpu().device == nullptr)
            {
                return;
            }
            if (desc.vertexShader == nullptr || desc.pixelShader == nullptr)
            {
                NS_LOG_ERROR(Graphics, "D3DMaterial: VertexShader / PixelShader が nullptr (共有参照必須)");
                return;
            }

            // サイズ 0 の指定は定数バッファを持たない意味なので作らない
            if (desc.constantBufferSize > 0u)
            {
                D3DBufferDesc cbDesc = MakeConstantBufferDesc(desc.constantBufferSize);
                std::unique_ptr<D3DBuffer> cb = D3DBuffer::Create(cbDesc);
                if (!cb->IsValid())
                {
                    NS_LOG_ERROR(Graphics, "D3DMaterial: ConstantBuffer 構築失敗 (byteSize={})", desc.constantBufferSize);
                    return;
                }
                m_cb = std::move(cb);
            }

            m_vertexShader = desc.vertexShader;
            m_pixelShader = desc.pixelShader;
            m_cbSlot = desc.cbSlot;
            m_valid = true;
        }

        D3DMaterial::~D3DMaterial() = default;

        bool D3DMaterial::IsValid() const noexcept
        {
            return m_valid;
        }

        bool D3DMaterial::IsUsingFallback() const noexcept
        {
            if (m_vertexShader == nullptr || m_pixelShader == nullptr)
            {
                return false;
            }
            return m_vertexShader->IsUsingFallback() || m_pixelShader->IsUsingFallback();
        }

        BlendMode D3DMaterial::Blend() const noexcept
        {
            return m_blend;
        }

        int D3DMaterial::RenderPriority() const noexcept
        {
            return m_renderPriority;
        }

        void D3DMaterial::SetTexture(uint slot, const D3DTexture* texture) noexcept
        {
            if (texture == nullptr)
            {
                m_textures.erase(slot);
                return;
            }
            m_textures[slot] = texture;
        }

        void D3DMaterial::UpdateParamsRaw(Renderer& renderer, const void* data, std::size_t bytes) noexcept
        {
            if (!IsValid() || !m_cb)
            {
                return;
            }
            renderer.Commands().UpdateSubresource(*m_cb, data, bytes);
        }

        void D3DMaterial::Bind(Renderer& renderer) noexcept
        {
            if (!IsValid())
            {
                return;
            }

            renderer.Commands().VSSetShader(*m_vertexShader);
            renderer.Commands().PSSetShader(*m_pixelShader);

            for (std::pair<const uint, const D3DTexture*>& entry : m_textures)
            {
                renderer.Commands().PSSetShaderResource(*entry.second, entry.first);
            }

            if (m_cb)
            {
                renderer.Commands().VSSetConstantBuffer(*m_cb, m_cbSlot);
                renderer.Commands().PSSetConstantBuffer(*m_cb, m_cbSlot);
            }

            // テクスチャのサンプリング設定は、描画時にシステム共通のものを利用する
            if (ID3D11SamplerState* sampler = renderer.States().LinearWrap())
            {
                renderer.Commands().PSSetSampler(sampler, 0u);
            }
        }

        void D3DMaterial::CreateInputLayoutFor(D3DMesh& mesh) noexcept
        {
            if (m_vertexShader != nullptr)
            {
                mesh.CreateInputLayout(*m_vertexShader);
            }
        }
    } // namespace Gfx
} // namespace NS
