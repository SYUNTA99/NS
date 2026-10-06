#include "NSlib/Graphics/Material.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/Buffer.h"
#include "NSlib/Graphics/CommandList.h"
#include "NSlib/Graphics/CommonStates.h"
#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/Mesh.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Graphics/Shader.h"
#include "NSlib/Graphics/Texture.h"

#include <map>

namespace NS::Gfx
{

    std::unique_ptr<Material> Material::Create(const MaterialDesc& desc)
    {
        return std::unique_ptr<Material>(new Material(desc));
    }

    Material::Material(const MaterialDesc& desc)
    {
        // 構築に失敗した場合でも後から参照できるように、設定値を先に保存しておく
        m_blend = desc.blend;
        m_renderPriority = desc.renderPriority;

        if (Gpu().device == nullptr)
        {
            NS_LOG_ERROR(Graphics, "Material: グローバル Device が無効");
            return;
        }
        if (desc.vertexShader == nullptr || desc.pixelShader == nullptr)
        {
            NS_LOG_ERROR(Graphics, "Material: VertexShader / PixelShader が nullptr (共有参照必須)");
            return;
        }

        // サイズ 0 の指定は定数バッファを持たない意味なので作らない
        if (desc.constantBufferSize > 0u)
        {
            BufferDesc cbDesc = MakeConstantBufferDesc(desc.constantBufferSize);
            std::unique_ptr<Buffer> cb = Buffer::Create(cbDesc);
            if (!cb->IsValid())
            {
                NS_LOG_ERROR(Graphics, "Material: ConstantBuffer 構築失敗 (byteSize={})", desc.constantBufferSize);
                return;
            }
            m_cb = std::move(cb);
        }

        m_vertexShader = desc.vertexShader;
        m_pixelShader = desc.pixelShader;
        m_cbSlot = desc.cbSlot;
        m_valid = true;
    }

    Material::~Material() = default;

    bool Material::IsValid() const noexcept
    {
        return m_valid;
    }

    bool Material::IsUsingFallback() const noexcept
    {
        if (m_vertexShader == nullptr || m_pixelShader == nullptr)
        {
            return false;
        }
        return m_vertexShader->IsUsingFallback() || m_pixelShader->IsUsingFallback();
    }

    BlendMode Material::Blend() const noexcept
    {
        return m_blend;
    }

    int Material::RenderPriority() const noexcept
    {
        return m_renderPriority;
    }

    void Material::SetTexture(unsigned slot, const Texture* texture) noexcept
    {
        if (texture == nullptr)
        {
            m_textures.erase(slot);
            return;
        }
        m_textures[slot] = texture;
    }

    void Material::UpdateParamsRaw(Renderer& renderer, const void* data, std::size_t bytes) noexcept
    {
        if (!IsValid() || !m_cb)
        {
            return;
        }
        renderer.Commands().UpdateSubresource(*m_cb, data, bytes);
    }

    void Material::Bind(Renderer& renderer) noexcept
    {
        if (!IsValid())
        {
            return;
        }

        renderer.Commands().VSSetShader(*m_vertexShader);
        renderer.Commands().PSSetShader(*m_pixelShader);

        for (std::pair<const unsigned, const Texture*>& entry : m_textures)
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

    void Material::CreateInputLayoutFor(Mesh& mesh) noexcept
    {
        if (m_vertexShader != nullptr)
        {
            mesh.CreateInputLayout(*m_vertexShader);
        }
    }

} // namespace NS::Gfx
