#include "NSlib/Gfx/Render/SceneDepthCopy.h"

#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DTexture.h"

namespace NS
{
    namespace Gfx
    {
        SceneDepthCopy::SceneDepthCopy() noexcept = default;
        SceneDepthCopy::~SceneDepthCopy() = default;

        SceneDepthCopy::CaptureResult SceneDepthCopy::Capture() noexcept
        {
            ++m_captureCount;
            ID3D11DeviceContext* context = Gpu().context;
            ComPtr<ID3D11DepthStencilView> dsv;
            if (context != nullptr)
            {
                context->OMGetRenderTargets(0, nullptr, dsv.GetAddressOf());
            }
            if (dsv == nullptr)
            {
                return CaptureResult{};
            }
            D3D11_TEXTURE2D_DESC desc{};
            const ComPtr<ID3D11Texture2D> source = ViewTexture2D(dsv.Get(), &desc);
            if (!source)
            {
                return CaptureResult{.failure = "奥行きが 2D のテクスチャでない"};
            }
            // 写し先は R24G8 の組で作るので、写せるのは同じ組で多重サンプルでない奥行きだけ
            const bool copyable =
                (desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || desc.Format == DXGI_FORMAT_R24G8_TYPELESS) &&
                desc.SampleDesc.Count == 1;
            if (!copyable)
            {
                return CaptureResult{.failure = "写せない奥行きの書式", .width = desc.Width, .height = desc.Height};
            }
            Copy* copy = CopyFor(desc.Width, desc.Height);
            if (copy == nullptr)
            {
                return CaptureResult{.failure = "奥行きの写し先を作れなかった", .width = desc.Width, .height = desc.Height};
            }
            // 書き込み中の奥行きを読み込み元に重ねないため、別のテクスチャへ写して読む
            context->CopyResource(copy->texture->Native(), source.Get());
            copy->lastUsed = m_captureCount;
            return CaptureResult{.texture = copy->texture.get(),
                                 .textureId = copy->id,
                                 .width = desc.Width,
                                 .height = desc.Height};
        }

        SceneDepthCopy::Copy* SceneDepthCopy::CopyFor(uint32 width, uint32 height) noexcept
        {
            Copy* oldest = &m_copies[0];
            for (Copy& copy : m_copies)
            {
                if (copy.texture != nullptr && copy.width == width && copy.height == height)
                {
                    return &copy;
                }
                if (copy.lastUsed < oldest->lastUsed)
                {
                    oldest = &copy;
                }
            }
            for (Copy& copy : m_copies)
            {
                if (copy.texture == nullptr)
                {
                    oldest = &copy;
                    break;
                }
            }
            *oldest = Copy{};
            D3DTextureDesc desc{};
            desc.width = width;
            desc.height = height;
            desc.format = DXGI_FORMAT_R24G8_TYPELESS;
            desc.viewFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            desc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
            std::unique_ptr<D3DTexture> texture = D3DTexture::Create(desc);
            if (texture == nullptr || texture->Srv() == nullptr)
            {
                return nullptr;
            }
            ++m_copiesCreated;
            oldest->texture = std::move(texture);
            oldest->id = m_copiesCreated;
            oldest->width = width;
            oldest->height = height;
            return oldest;
        }
    } // namespace Gfx
} // namespace NS
