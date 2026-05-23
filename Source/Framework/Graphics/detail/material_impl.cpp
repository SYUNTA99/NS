#include "Framework/Graphics/Material.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommonStates.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/ShaderProgram.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <map>
#include <utility>

namespace NS::Graphics
{
    using detail::ComPtr;

    struct Material::Impl
    {
        ShaderProgram* shader = nullptr;
        std::map<unsigned, const Texture*> textures;
        std::unique_ptr<ConstantBuffer> cb;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11SamplerState> sampler;
        unsigned cbSlot = 1;
        ShaderStage cbStages = ShaderStage::Vertex | ShaderStage::Pixel;
        bool valid = false;
    };

    Material::Material(Renderer& renderer, const MaterialDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        auto* device = detail::GetDevice(renderer);
        auto* context = detail::GetContext(renderer);
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Material: Renderer の Device / Context が無効");
            return;
        }
        if (desc.shader == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Material: ShaderProgram が nullptr ( 共有参照必須)");
            return;
        }

        if (desc.constantBufferSize > 0u)
        {
            auto cb = std::make_unique<ConstantBuffer>(renderer, desc.constantBufferSize);
            if (!cb->IsValid())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Material: ConstantBuffer 構築失敗 (byteSize={})",
                             desc.constantBufferSize);
                return;
            }
            m_pImpl->cb = std::move(cb);
        }

        m_pImpl->shader = desc.shader;
        m_pImpl->context = context;
        m_pImpl->sampler = static_cast<ID3D11SamplerState*>(renderer.States().LinearWrap());
        m_pImpl->cbSlot = desc.cbSlot;
        m_pImpl->cbStages = desc.cbStages;
        m_pImpl->valid = true;
    }

    Material::~Material() = default;

    bool Material::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->valid;
    }

    bool Material::IsUsingFallback() const noexcept
    {
        if (!m_pImpl || m_pImpl->shader == nullptr)
        {
            return false;
        }
        return m_pImpl->shader->IsUsingFallback();
    }

    void Material::SetTexture(unsigned slot, const Texture* texture) noexcept
    {
        if (!m_pImpl)
        {
            return;
        }
        if (texture == nullptr)
        {
            m_pImpl->textures.erase(slot);
            return;
        }
        m_pImpl->textures[slot] = texture;
    }

    void Material::ClearTexture(unsigned slot) noexcept
    {
        if (!m_pImpl)
        {
            return;
        }
        m_pImpl->textures.erase(slot);
    }

    void Material::UpdateParamsRaw(const void* data, std::size_t bytes) noexcept
    {
        if (!IsValid() || !m_pImpl->cb)
        {
            return;
        }
        m_pImpl->cb->UpdateRaw(data, bytes);
    }

    void Material::Bind() noexcept
    {
        if (!IsValid())
        {
            return;
        }

        m_pImpl->shader->Bind();

        for (auto& [slot, tex] : m_pImpl->textures)
        {
            if (tex != nullptr)
            {
                tex->Bind(slot, ShaderStage::Pixel);
            }
        }

        if (m_pImpl->cb)
        {
            m_pImpl->cb->Bind(m_pImpl->cbSlot, m_pImpl->cbStages);
        }

        if (m_pImpl->sampler)
        {
            ID3D11SamplerState* samplers[1] = {m_pImpl->sampler.Get()};
            m_pImpl->context->PSSetSamplers(0u, 1u, samplers);
        }
    }

} // namespace NS::Graphics
