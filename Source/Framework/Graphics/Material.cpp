#include "Framework/Graphics/Material.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/CommonStates.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/Texture.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <map>
#include <utility>

namespace NS::Graphics
{

    std::unique_ptr<Material> Material::Create(const MaterialDesc& desc)
    {
        return std::unique_ptr<Material>(new Material(desc));
    }

    Material::Material(const MaterialDesc& desc)
    {
        // blend / renderPriority は GPU
        // リソースに依らない分類メタデータなので、構築失敗時でも参照できるよう先に保持する
        m_blend = desc.blend;
        m_renderPriority = desc.renderPriority;

        if (Gpu().device == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Material: グローバル Device が無効");
            return;
        }
        if (desc.vertexShader == nullptr || desc.pixelShader == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Material: VertexShader / PixelShader が nullptr (共有参照必須)");
            return;
        }

        if (desc.constantBufferSize > 0u)
        {
            BufferDesc cbDesc = MakeConstantBufferDesc(desc.constantBufferSize);
            std::unique_ptr<Buffer> cb = Buffer::Create(cbDesc);
            if (!cb->IsValid())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Material: ConstantBuffer 構築失敗 (byteSize={})",
                             desc.constantBufferSize);
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

    void Material::ClearTexture(unsigned slot) noexcept
    {
        m_textures.erase(slot);
    }

    void Material::UpdateParamsRaw(Renderer& renderer, const void* data, std::size_t bytes) noexcept
    {
        if (!IsValid() || !m_cb)
        {
            return;
        }
        renderer.Commands().UpdateBuffer(*m_cb, data, bytes);
    }

    void Material::Bind(Renderer& renderer) noexcept
    {
        if (!IsValid())
        {
            return;
        }

        renderer.Commands().SetShader(*m_vertexShader);
        renderer.Commands().SetShader(*m_pixelShader);

        for (auto& [slot, tex] : m_textures)
        {
            if (tex != nullptr)
            {
                renderer.Commands().SetTexture(*tex, slot, ShaderType::Pixel);
            }
        }

        if (m_cb)
        {
            renderer.Commands().SetConstantBuffer(*m_cb, m_cbSlot, ShaderType::Vertex);
            renderer.Commands().SetConstantBuffer(*m_cb, m_cbSlot, ShaderType::Pixel);
        }

        // 共有 LinearWrap は Renderer の CommonStates から bind 時に取得する
        if (auto* sampler = renderer.States().LinearWrap())
        {
            renderer.Commands().SetSampler(sampler, 0u, ShaderType::Pixel);
        }
    }

    void Material::CreateInputLayoutFor(Mesh& mesh) noexcept
    {
        if (m_vertexShader != nullptr)
        {
            mesh.CreateInputLayout(*m_vertexShader);
        }
    }

} // namespace NS::Graphics
