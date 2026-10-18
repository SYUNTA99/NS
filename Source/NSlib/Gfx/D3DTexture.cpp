#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/detail/TextureFile.h"
#include "NSlib/Windows/Filesystem.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <algorithm>

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            [[nodiscard]] ComPtr<ID3D11Texture2D> AsTexture2D(ID3D11Resource* resource) noexcept
            {
                ComPtr<ID3D11Texture2D> tex2d;
                if (resource != nullptr)
                {
                    const HRESULT hr = resource->QueryInterface(IID_PPV_ARGS(tex2d.GetAddressOf()));
                    if (FAILED(hr))
                    {
                        NS_LOG_ERROR(Graphics,
                                     "D3DTexture: ID3D11Texture2D への QI 失敗、 2D 以外のリソースの可能性 (hr=0x{:X})",
                                     static_cast<uint>(hr));
                    }
                }
                return tex2d;
            }

            [[nodiscard]] Size2D Texture2DSize(ID3D11Texture2D* tex2d) noexcept
            {
                if (tex2d == nullptr)
                {
                    return Size2D{0, 0};
                }
                D3D11_TEXTURE2D_DESC d{};
                tex2d->GetDesc(&d);
                return Size2D{static_cast<int>(d.Width), static_cast<int>(d.Height)};
            }

            // bindFlags に立っているビューだけを作る
            void CreateRequestedViews(ID3D11Device* device,
                                      ID3D11Texture2D* tex2d,
                                      UINT bindFlags,
                                      DXGI_FORMAT viewFormat,
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
                    // 書式を渡されなければ資源の書式のまま作る
                    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
                    const D3D11_SHADER_RESOURCE_VIEW_DESC* viewDescPtr = nullptr;
                    if (viewFormat != DXGI_FORMAT_UNKNOWN)
                    {
                        viewDesc.Format = viewFormat;
                        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        viewDesc.Texture2D.MostDetailedMip = 0;
                        viewDesc.Texture2D.MipLevels = 1;
                        viewDescPtr = &viewDesc;
                    }
                    const HRESULT hr = device->CreateShaderResourceView(tex2d, viewDescPtr, outSrv.GetAddressOf());
                    if (FAILED(hr))
                    {
                        NS_LOG_ERROR(
                            Graphics, "D3DTexture: CreateShaderResourceView 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    }
                }
                if (bindFlags & D3D11_BIND_RENDER_TARGET)
                {
                    const HRESULT hr = device->CreateRenderTargetView(tex2d, nullptr, outRtv.GetAddressOf());
                    if (FAILED(hr))
                    {
                        NS_LOG_ERROR(
                            Graphics, "D3DTexture: CreateRenderTargetView 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    }
                }
                if (bindFlags & D3D11_BIND_DEPTH_STENCIL)
                {
                    const HRESULT hr = device->CreateDepthStencilView(tex2d, nullptr, outDsv.GetAddressOf());
                    if (FAILED(hr))
                    {
                        NS_LOG_ERROR(
                            Graphics, "D3DTexture: CreateDepthStencilView 失敗 (hr=0x{:X})", static_cast<uint>(hr));
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
                const uint8 magenta[4] = {0xFF, 0x00, 0xFF, 0xFF};

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
                    NS_LOG_ERROR(
                        Graphics, "D3DTexture fallback CreateTexture2D 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }

                hr = device->CreateShaderResourceView(tex2d.Get(), nullptr, outSrv.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(Graphics, "D3DTexture fallback CreateSRV 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }

                outTex = tex2d;
                return true;
            }

            bool TryLoadDds(ID3D11Device* device,
                            ID3D11DeviceContext* context,
                            const uint8* bytes,
                            std::size_t size,
                            ComPtr<ID3D11Resource>& outResource,
                            ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
            {
                const HRESULT hr = DirectX::CreateDDSTextureFromMemory(
                    device, context, bytes, size, outResource.GetAddressOf(), outSrv.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(Graphics, "CreateDDSTextureFromMemory 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }
                return true;
            }

            // mipmap は常に作る。RENDER_TARGET と GENERATE_MIPS は GenerateMips の前提
            // sRGB へ強制しない。NS はどこでも色空間を変換しておらず、Skybox も IGNORE_SRGB で読む
            bool TryLoadWic(ID3D11Device* device,
                            ID3D11DeviceContext* context,
                            const uint8* bytes,
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
                    NS_LOG_ERROR(Graphics, "CreateWICTextureFromMemoryEx 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }
                return true;
            }

            // 中身の無いテクスチャを作る。mipmap 1 枚・単層のみ
            bool CreateEmptyTexture(ID3D11Device* device,
                                    const D3DTextureDesc& desc,
                                    ComPtr<ID3D11Texture2D>& outTex) noexcept
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
                                 "D3DTexture: CreateTexture2D 失敗 (W={} H={} hr=0x{:X})",
                                 desc.width,
                                 desc.height,
                                 static_cast<uint>(hr));
                    return false;
                }
                return true;
            }
        } // namespace

        std::unique_ptr<D3DTexture> D3DTexture::Create(const D3DTextureDesc& desc)
        {
            return std::unique_ptr<D3DTexture>(new D3DTexture(desc));
        }

        std::unique_ptr<D3DTexture> D3DTexture::Create(ComPtr<ID3D11Texture2D> existing, UINT bindFlags)
        {
            return std::unique_ptr<D3DTexture>(new D3DTexture(std::move(existing), bindFlags));
        }

        D3DTexture::D3DTexture(const D3DTextureDesc& desc)
        {
            ID3D11Device* device = Gpu().device;
            ID3D11DeviceContext* context = Gpu().context;
            // D3DDevice の無い場 (GPU の無い試し) では、ERROR を出さずに空のテクスチャを返す
            // ゲームでは D3DDevice を先に作るので、ここで返らない
            if (device == nullptr || context == nullptr)
            {
                return;
            }

            // 生成の失敗はフォールバックへ差し替えない。描画先を頼んだ側が IsValid で弾いて畳む
            // 大きさが 0 の時はここへ入らずフォールバックへ落ちる。マテリアルへ差すダミーがこの形
            if (desc.path.empty() && desc.width > 0 && desc.height > 0)
            {
                if (CreateEmptyTexture(device, desc, m_tex))
                {
                    CreateRequestedViews(device, m_tex.Get(), desc.bindFlags, desc.viewFormat, m_srv, m_rtv, m_dsv);
                    m_size = Size2D{static_cast<int>(desc.width), static_cast<int>(desc.height)};
                }
                return;
            }

            ComPtr<ID3D11Resource> resource;
            bool loaded = false;
            if (!desc.path.empty())
            {
                std::optional<std::vector<std::byte>> bytesOpt = OS::FileSystem::ReadAllBytes(desc.path);
                if (bytesOpt.has_value())
                {
                    const std::vector<std::byte>& bytes = bytesOpt.value();
                    const uint8* data = reinterpret_cast<const uint8*>(bytes.data());
                    if (detail::IsDdsExtension(desc.path))
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
                    NS_LOG_ERROR(Graphics, "D3DTexture load failed: {}", desc.path);
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
            m_size = Size2D{1, 1};
            m_fallback = true;
        }

        D3DTexture::D3DTexture(ComPtr<ID3D11Texture2D> existing, UINT bindFlags)
        {
            ID3D11Device* device = Gpu().device;
            if (device == nullptr || !existing)
            {
                NS_LOG_ERROR(Graphics, "D3DTexture: ラップ対象 / Device が無効");
                return;
            }
            m_tex = std::move(existing);
            m_size = Texture2DSize(m_tex.Get());
            CreateRequestedViews(device, m_tex.Get(), bindFlags, DXGI_FORMAT_UNKNOWN, m_srv, m_rtv, m_dsv);
        }

        D3DTexture::~D3DTexture() = default;

        bool D3DTexture::IsValid() const noexcept
        {
            return m_srv || m_rtv || m_dsv;
        }
        Size2D D3DTexture::Size() const noexcept
        {
            return m_size;
        }
        bool D3DTexture::IsUsingFallback() const noexcept
        {
            return m_fallback;
        }
        ID3D11Texture2D* D3DTexture::Native() const noexcept
        {
            return m_tex.Get();
        }
        ID3D11ShaderResourceView* D3DTexture::Srv() const noexcept
        {
            return m_srv.Get();
        }
        ID3D11RenderTargetView* D3DTexture::Rtv() const noexcept
        {
            return m_rtv.Get();
        }
        ID3D11DepthStencilView* D3DTexture::Dsv() const noexcept
        {
            return m_dsv.Get();
        }
    } // namespace Gfx
} // namespace NS
