#include "Runtime/Graphics/RenderTarget.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Texture.h"

namespace NS::Graphics
{

    std::unique_ptr<RenderTarget> RenderTarget::Create(::NS::Core::Size2D size)
    {
        return std::unique_ptr<RenderTarget>(new RenderTarget(size));
    }

    RenderTarget::RenderTarget(::NS::Core::Size2D size)
    {
        Build(size);
    }

    RenderTarget::~RenderTarget() = default;

    void RenderTarget::Build(::NS::Core::Size2D size) noexcept
    {
        if (size.width <= 0 || size.height <= 0)
        {
            NS_LOG_ERROR(Graphics, "RenderTarget: サイズが不正 ({}x{})", size.width, size.height);
            return;
        }

        TextureCreateDesc colorDesc{};
        colorDesc.width = static_cast<UINT>(size.width);
        colorDesc.height = static_cast<UINT>(size.height);
        colorDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        std::unique_ptr<Texture> color = Texture::Create(colorDesc);
        if (color->Rtv() == nullptr || color->Srv() == nullptr)
        {
            NS_LOG_ERROR(Graphics, "RenderTarget: カラーの構築失敗 ({}x{})", size.width, size.height);
            return;
        }

        // 深度は backbuffer 側と同じ形式で揃える
        TextureCreateDesc depthDesc{};
        depthDesc.width = static_cast<UINT>(size.width);
        depthDesc.height = static_cast<UINT>(size.height);
        depthDesc.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.bindFlags = D3D11_BIND_DEPTH_STENCIL;

        std::unique_ptr<Texture> depth = Texture::Create(depthDesc);
        if (depth->Dsv() == nullptr)
        {
            NS_LOG_ERROR(Graphics, "RenderTarget: 深度の構築失敗 ({}x{})", size.width, size.height);
            return;
        }

        m_color = std::move(color);
        m_depth = std::move(depth);
    }

    bool RenderTarget::IsValid() const noexcept
    {
        return m_color != nullptr && m_depth != nullptr;
    }

    ::NS::Core::Size2D RenderTarget::Size() const noexcept
    {
        if (!m_color)
        {
            return ::NS::Core::Size2D{0, 0};
        }
        return m_color->Size();
    }

    void RenderTarget::Resize(::NS::Core::Size2D size) noexcept
    {
        if (size.width <= 0 || size.height <= 0)
        {
            return;
        }
        if (size == Size())
        {
            return;
        }
        m_color.reset();
        m_depth.reset();
        Build(size);
    }

    Texture* RenderTarget::Color() const noexcept
    {
        return m_color.get();
    }

    Texture* RenderTarget::Depth() const noexcept
    {
        return m_depth.get();
    }

    void* RenderTarget::UiTextureHandle() const noexcept
    {
        if (!m_color)
        {
            return nullptr;
        }
        return m_color->Srv();
    }

} // namespace NS::Graphics
