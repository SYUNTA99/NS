#include "Runtime/Graphics/Texture.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Platform/Filesystem.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <algorithm>

namespace NS::Gfx
{

    namespace
    {
        [[nodiscard]] bool IsDdsExtension(std::string_view path)
        {
            std::string ext = NS::Platform::FileSystem::Extension(path);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext == ".dds";
        }

        [[nodiscard]] ComPtr<ID3D11Texture2D> AsTexture2D(ID3D11Resource* resource) noexcept
        {
            ComPtr<ID3D11Texture2D> tex2d;
            if (resource != nullptr)
            {
                const HRESULT hr = resource->QueryInterface(IID_PPV_ARGS(tex2d.GetAddressOf()));
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(Graphics,
                                 "Texture: ID3D11Texture2D への QI 失敗、 2D 以外のリソースの可能性 (hr=0x{:X})",
                                 static_cast<unsigned>(hr));
                }
            }
            return tex2d;
        }

        [[nodiscard]] ::NS::Core::Size2D Texture2DSize(ID3D11Texture2D* tex2d) noexcept
        {
            if (tex2d == nullptr)
            {
                return ::NS::Core::Size2D{0, 0};
            }
            D3D11_TEXTURE2D_DESC d{};
            tex2d->GetDesc(&d);
            return ::NS::Core::Size2D{static_cast<int>(d.Width), static_cast<int>(d.Height)};
        }

        // bindFlags に立っているビューだけを作る
        void CreateRequestedViews(ID3D11Device* device,
                                  ID3D11Texture2D* tex2d,
                                  UINT bindFlags,
                                  ComPtr<ID3D11ShaderResourceView>& outSrv,
                                  ComPtr<ID3D11RenderTargetView>& outRtv,
                                  ComPtr<ID3D11DepthStencilView>& outDsv) noexcept
        {
            if (device == nullptr || tex2d == nullptr)
            {
                return;
            }
            if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
            {
                const HRESULT hr = device->CreateShaderResourceView(tex2d, nullptr, outSrv.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(
                        Graphics, "Texture: CreateShaderResourceView 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                }
            }
            if (bindFlags & D3D11_BIND_RENDER_TARGET)
            {
                const HRESULT hr = device->CreateRenderTargetView(tex2d, nullptr, outRtv.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(
                        Graphics, "Texture: CreateRenderTargetView 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                }
            }
            if (bindFlags & D3D11_BIND_DEPTH_STENCIL)
            {
                const HRESULT hr = device->CreateDepthStencilView(tex2d, nullptr, outDsv.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(
                        Graphics, "Texture: CreateDepthStencilView 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                }
            }
        }

        bool CreateMagentaFallback(ID3D11Device* device,
                                   ComPtr<ID3D11Texture2D>& outTex,
                                   ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
            if (device == nullptr)
            {
                return false;
            }
            const std::uint8_t magenta[4] = {0xFF, 0x00, 0xFF, 0xFF};

            D3D11_TEXTURE2D_DESC td{};
            td.Width = 1;
            td.Height = 1;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA srd{};
            srd.pSysMem = magenta;
            srd.SysMemPitch = sizeof(magenta);

            ComPtr<ID3D11Texture2D> tex2d;
            HRESULT hr = device->CreateTexture2D(&td, &srd, tex2d.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "Texture fallback CreateTexture2D 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }

            hr = device->CreateShaderResourceView(tex2d.Get(), nullptr, outSrv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "Texture fallback CreateSRV 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }

            outTex = tex2d;
            return true;
        }

        bool TryLoadDds(ID3D11Device* device,
                        ID3D11DeviceContext* context,
                        const std::uint8_t* bytes,
                        std::size_t size,
                        ComPtr<ID3D11Resource>& outResource,
                        ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
            const HRESULT hr = DirectX::CreateDDSTextureFromMemory(
                device, context, bytes, size, outResource.GetAddressOf(), outSrv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "CreateDDSTextureFromMemory 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        // mipmap は常に作る。RENDER_TARGET と GENERATE_MIPS は GenerateMips の前提
        // sRGB へ強制しない。NS はどこでも色空間を変換しておらず、Skybox も IGNORE_SRGB で読む
        bool TryLoadWic(ID3D11Device* device,
                        ID3D11DeviceContext* context,
                        const std::uint8_t* bytes,
                        std::size_t size,
                        ComPtr<ID3D11Resource>& outResource,
                        ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
            const HRESULT hr =
                DirectX::CreateWICTextureFromMemoryEx(device,
                                                      context,
                                                      bytes,
                                                      size,
                                                      0u,
                                                      D3D11_USAGE_DEFAULT,
                                                      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
                                                      0u,
                                                      D3D11_RESOURCE_MISC_GENERATE_MIPS,
                                                      DirectX::WIC_LOADER_IGNORE_SRGB,
                                                      outResource.GetAddressOf(),
                                                      outSrv.GetAddressOf());

            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "CreateWICTextureFromMemoryEx 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        // 中身の無いテクスチャを作る。mipmap 1 枚・単層のみ
        bool CreateEmptyTexture(ID3D11Device* device, const TextureDesc& desc, ComPtr<ID3D11Texture2D>& outTex) noexcept
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = desc.width;
            td.Height = desc.height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = desc.format;
            td.SampleDesc.Count = 1;
            td.SampleDesc.Quality = 0;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = desc.bindFlags;

            const HRESULT hr = device->CreateTexture2D(&td, nullptr, outTex.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics,
                             "Texture: CreateTexture2D 失敗 (W={} H={} hr=0x{:X})",
                             desc.width,
                             desc.height,
                             static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }
    } // namespace

    std::unique_ptr<Texture> Texture::Create(const TextureDesc& desc)
    {
        return std::unique_ptr<Texture>(new Texture(desc));
    }

    std::unique_ptr<Texture> Texture::Create(ComPtr<ID3D11Texture2D> existing, UINT bindFlags)
    {
        return std::unique_ptr<Texture>(new Texture(std::move(existing), bindFlags));
    }

    Texture::Texture(const TextureDesc& desc)
    {
        ID3D11Device* device = Gpu().device;
        ID3D11DeviceContext* context = Gpu().context;
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(Graphics, "Texture: Renderer の Device / Context が無効");
            return;
        }

        // 生成の失敗はフォールバックへ差し替えない。描画先を頼んだ側が IsValid で弾いて畳む
        // 大きさが 0 の時はここへ入らずフォールバックへ落ちる。マテリアルへ差すダミーがこの形
        if (desc.path.empty() && desc.width > 0 && desc.height > 0)
        {
            if (CreateEmptyTexture(device, desc, m_tex))
            {
                CreateRequestedViews(device, m_tex.Get(), desc.bindFlags, m_srv, m_rtv, m_dsv);
                m_size = ::NS::Core::Size2D{static_cast<int>(desc.width), static_cast<int>(desc.height)};
            }
            return;
        }

        ComPtr<ID3D11Resource> resource;
        bool loaded = false;
        if (!desc.path.empty())
        {
            auto bytesOpt = ::NS::Platform::FileSystem::ReadAllBytes(desc.path);
            if (bytesOpt.has_value())
            {
                const auto& bytes = bytesOpt.value();
                const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
                if (IsDdsExtension(desc.path))
                {
                    // DDS は内蔵 mipmap を尊重して作り直さない
                    // mipmap が欲しければ Texconv.exe 等で事前生成した DDS を渡すこと
                    loaded = TryLoadDds(device, context, data, bytes.size(), resource, m_srv);
                }
                else
                {
                    loaded = TryLoadWic(device, context, data, bytes.size(), resource, m_srv);
                }
            }
            else
            {
                NS_LOG_ERROR(Graphics, "Texture load failed: {}", desc.path);
            }
        }

        if (loaded)
        {
            m_tex = AsTexture2D(resource.Get());
            m_size = Texture2DSize(m_tex.Get());
            return;
        }

        if (!CreateMagentaFallback(device, m_tex, m_srv))
        {
            return;
        }
        m_size = ::NS::Core::Size2D{1, 1};
        m_fallback = true;
    }

    Texture::Texture(ComPtr<ID3D11Texture2D> existing, UINT bindFlags)
    {
        ID3D11Device* device = Gpu().device;
        if (device == nullptr || !existing)
        {
            NS_LOG_ERROR(Graphics, "Texture: ラップ対象 / Device が無効");
            return;
        }
        m_tex = std::move(existing);
        m_size = Texture2DSize(m_tex.Get());
        CreateRequestedViews(device, m_tex.Get(), bindFlags, m_srv, m_rtv, m_dsv);
    }

    Texture::~Texture() = default;

    bool Texture::IsValid() const noexcept
    {
        return m_srv || m_rtv || m_dsv;
    }
    ::NS::Core::Size2D Texture::Size() const noexcept
    {
        return m_size;
    }
    bool Texture::IsUsingFallback() const noexcept
    {
        return m_fallback;
    }
    ID3D11Texture2D* Texture::Native() const noexcept
    {
        return m_tex.Get();
    }
    ID3D11ShaderResourceView* Texture::Srv() const noexcept
    {
        return m_srv.Get();
    }
    ID3D11RenderTargetView* Texture::Rtv() const noexcept
    {
        return m_rtv.Get();
    }
    ID3D11DepthStencilView* Texture::Dsv() const noexcept
    {
        return m_dsv.Get();
    }

} // namespace NS::Gfx
