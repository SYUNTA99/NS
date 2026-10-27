#include "NSlib/Gfx/D3DRenderTarget.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3DTexture.h"

namespace NS::Gfx
{
        std::unique_ptr<D3DRenderTarget> D3DRenderTarget::Create(Size2D size)
        {
            return std::unique_ptr<D3DRenderTarget>(new D3DRenderTarget(size));
        }

        D3DRenderTarget::D3DRenderTarget(Size2D size)
        {
            Build(size);
        }

        D3DRenderTarget::~D3DRenderTarget() = default;

        void D3DRenderTarget::Build(Size2D size) noexcept
        {
            if (size.width <= 0 || size.height <= 0)
            {
                NS_LOG_ERROR(Graphics, "D3DRenderTarget: サイズが不正 ({}x{})", size.width, size.height);
                return;
            }

            D3DTextureDesc colorDesc{};
            colorDesc.width = static_cast<UINT>(size.width);
            colorDesc.height = static_cast<UINT>(size.height);
            colorDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            colorDesc.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            std::unique_ptr<D3DTexture> color = D3DTexture::Create(colorDesc);
            if (color->Rtv() == nullptr || color->Srv() == nullptr)
            {
                NS_LOG_ERROR(Graphics, "D3DRenderTarget: カラーの構築失敗 ({}x{})", size.width, size.height);
                return;
            }

            // 深度は backbuffer 側と同じ形式で揃える
            D3DTextureDesc depthDesc{};
            depthDesc.width = static_cast<UINT>(size.width);
            depthDesc.height = static_cast<UINT>(size.height);
            depthDesc.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthDesc.bindFlags = D3D11_BIND_DEPTH_STENCIL;

            std::unique_ptr<D3DTexture> depth = D3DTexture::Create(depthDesc);
            if (depth->Dsv() == nullptr)
            {
                NS_LOG_ERROR(Graphics, "D3DRenderTarget: 深度の構築失敗 ({}x{})", size.width, size.height);
                return;
            }

            m_color = std::move(color);
            m_depth = std::move(depth);
        }

        bool D3DRenderTarget::IsValid() const noexcept { return m_color != nullptr && m_depth != nullptr; }

        Size2D D3DRenderTarget::Size() const noexcept
        {
            if (!m_color)
            {
                return Size2D{0, 0};
            }
            return m_color->Size();
        }

        void D3DRenderTarget::Resize(Size2D size) noexcept
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

        D3DTexture* D3DRenderTarget::Color() const noexcept { return m_color.get(); }

        D3DTexture* D3DRenderTarget::Depth() const noexcept { return m_depth.get(); }

        void* D3DRenderTarget::UiTextureHandle() const noexcept
        {
            if (!m_color)
            {
                return nullptr;
            }
            return m_color->Srv();
        }
} // namespace NS::Gfx
