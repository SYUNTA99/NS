#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DCommonStates.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DTexture.h"

#include <algorithm>
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

            // D3DDevice の無い場 (GPU の無い試し) では、ERROR を出さずに空のマテリアルを返す
            // ゲームでは D3DDevice を先に作るので、ここで返らない
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

            // 材質の値のバッファを作れなくても描く事はできるので、材質の値なしで描く
            if (desc.materialParamsSize > 0u)
            {
                std::unique_ptr<D3DBuffer> params = D3DBuffer::Create(MakeConstantBufferDesc(desc.materialParamsSize));
                if (params->IsValid())
                {
                    m_materialParams = std::move(params);
                    m_materialParamBytes.assign(desc.materialParamsSize, std::byte{0});
                    m_materialParamsDirty = true;
                }
                else
                {
                    NS_LOG_ERROR(Graphics,
                                 "D3DMaterial: 材質の値のバッファ ({} バイト) を作れないので、材質の値なしで描く",
                                 desc.materialParamsSize);
                }
            }

            m_vertexShader = desc.vertexShader;
            m_pixelShader = desc.pixelShader;
            m_valid = true;
        }

        D3DMaterial::~D3DMaterial() = default;

        bool D3DMaterial::IsValid() const noexcept { return m_valid; }

        bool D3DMaterial::IsUsingFallback() const noexcept
        {
            if (m_vertexShader == nullptr || m_pixelShader == nullptr)
            {
                return false;
            }
            return m_vertexShader->IsUsingFallback() || m_pixelShader->IsUsingFallback();
        }

        BlendMode D3DMaterial::Blend() const noexcept { return m_blend; }

        int D3DMaterial::RenderPriority() const noexcept { return m_renderPriority; }

        void D3DMaterial::SetTexture(uint slot, const D3DTexture* texture) noexcept
        {
            if (texture == nullptr)
            {
                m_textures.erase(slot);
                return;
            }
            m_textures[slot] = texture;
        }

        void D3DMaterial::UpdateParamsRaw(D3DDevice& device, const void* data, std::size_t bytes) noexcept
        {
            if (!IsValid() || !m_cb)
            {
                return;
            }
            device.Commands().UpdateSubresource(*m_cb, data, bytes);
        }

        void D3DMaterial::SetMaterialParams(std::span<const std::byte> bytes) noexcept
        {
            if (!IsValid() || !m_materialParams)
            {
                return;
            }
            NS_ASSERT(Graphics,
                      bytes.size() == m_materialParamBytes.size(),
                      "材質の値の大きさ {} がバッファの {} と違う",
                      bytes.size(),
                      m_materialParamBytes.size());
            if (bytes.size() != m_materialParamBytes.size())
            {
                return;
            }
            std::copy(bytes.begin(), bytes.end(), m_materialParamBytes.begin());
            m_materialParamsDirty = true;
        }

        void D3DMaterial::Bind(D3DDevice& device) noexcept
        {
            if (!IsValid())
            {
                return;
            }

            device.Commands().VSSetShader(*m_vertexShader);
            device.Commands().PSSetShader(*m_pixelShader);

            for (std::pair<const uint, const D3DTexture*>& entry : m_textures)
            {
                device.Commands().PSSetShaderResource(*entry.second, entry.first);
            }

            if (m_cb)
            {
                device.Commands().VSSetConstantBuffer(*m_cb, k_DrawConstantsSlot);
                device.Commands().PSSetConstantBuffer(*m_cb, k_DrawConstantsSlot);
            }
            if (m_materialParams)
            {
                if (m_materialParamsDirty)
                {
                    device.Commands().UpdateSubresource(
                        *m_materialParams, m_materialParamBytes.data(), m_materialParamBytes.size());
                    m_materialParamsDirty = false;
                }
                device.Commands().VSSetConstantBuffer(*m_materialParams, k_MaterialParamsSlot);
                device.Commands().PSSetConstantBuffer(*m_materialParams, k_MaterialParamsSlot);
            }

            // テクスチャのサンプリング設定は、描画時にシステム共通のものを利用する
            if (ID3D11SamplerState* sampler = device.States().LinearWrap())
            {
                device.Commands().PSSetSampler(sampler, 0u);
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
