#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/detail/TextureFile.h"
#include "NSlib/Core/Windows/Filesystem.h"

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

            // 立方体の 6 面を mipmap の全段で読むビューを作る
            [[nodiscard]] bool CreateCubeSrv(ID3D11Device* device,
                                             ID3D11Texture2D* cube,
                                             ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
            {
                D3D11_TEXTURE2D_DESC td{};
                cube->GetDesc(&td);
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = td.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                srvDesc.TextureCube.MostDetailedMip = 0;
                srvDesc.TextureCube.MipLevels = td.MipLevels;
                const HRESULT hr = device->CreateShaderResourceView(cube, &srvDesc, outSrv.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(Graphics, "D3DTexture: 立方体の CreateSRV 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }
                return true;
            }

            // 1×1 のピンク。cube なら 6 面とも同じ色の立方体
            bool CreateMagentaFallback(ID3D11Device* device,
                                       bool cube,
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
                if (cube)
                {
                    td.ArraySize = 6;
                    td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
                }

                std::array<D3D11_SUBRESOURCE_DATA, 6> srd{};
                for (D3D11_SUBRESOURCE_DATA& face : srd)
                {
                    face.pSysMem = magenta;
                    face.SysMemPitch = sizeof(magenta);
                }

                ComPtr<ID3D11Texture2D> tex2d;
                HRESULT hr = device->CreateTexture2D(&td, srd.data(), tex2d.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(
                        Graphics, "D3DTexture fallback CreateTexture2D 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }

                if (cube)
                {
                    if (!CreateCubeSrv(device, tex2d.Get(), outSrv))
                    {
                        return false;
                    }
                }
                else
                {
                    hr = device->CreateShaderResourceView(tex2d.Get(), nullptr, outSrv.GetAddressOf());
                    if (FAILED(hr))
                    {
                        NS_LOG_ERROR(
                            Graphics, "D3DTexture fallback CreateSRV 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                        return false;
                    }
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
            // sRGB へ強制しない。NS はどこでも色空間を変換しないので、2D も立方体の面も画像の値をそのまま読む
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

            // 画像ファイルを読む。DDS は中の mipmap を使い、ほかは WIC で読んで mipmap を作る
            bool LoadImageFile(ID3D11Device* device,
                               ID3D11DeviceContext* context,
                               const std::string& path,
                               ComPtr<ID3D11Resource>& outResource,
                               ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
            {
                std::optional<std::vector<std::byte>> bytesOpt = OS::FileSystem::ReadAllBytes(path);
                if (!bytesOpt.has_value())
                {
                    NS_LOG_ERROR(Graphics, "D3DTexture load failed: {}", path);
                    return false;
                }
                const std::vector<std::byte>& bytes = bytesOpt.value();
                const uint8* data = reinterpret_cast<const uint8*>(bytes.data());
                if (detail::IsDdsExtension(path))
                {
                    // DDS は内蔵 mipmap を尊重して作り直さない
                    // mipmap が欲しければ Texconv.exe 等で事前生成した DDS を渡すこと
                    return TryLoadDds(device, context, data, bytes.size(), outResource, outSrv);
                }
                return TryLoadWic(device, context, data, bytes.size(), outResource, outSrv);
            }

            // 6 面を 2D と同じ道で読み、各面の mipmap を全段そのまま立方体へ写す
            // mipmap を作るのは 2D の読み込みの 1 か所だけにするため、立方体では作り直さない
            bool LoadCubeFaces(ID3D11Device* device,
                               ID3D11DeviceContext* context,
                               const std::array<std::string, 6>& facePaths,
                               ComPtr<ID3D11Resource>& outResource,
                               ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
            {
                std::array<ComPtr<ID3D11Texture2D>, 6> faces{};
                D3D11_TEXTURE2D_DESC faceDesc{};
                for (std::size_t i = 0; i < faces.size(); ++i)
                {
                    ComPtr<ID3D11Resource> resource;
                    ComPtr<ID3D11ShaderResourceView> faceSrv;
                    if (!LoadImageFile(device, context, facePaths[i], resource, faceSrv))
                    {
                        return false;
                    }
                    faces[i] = AsTexture2D(resource.Get());
                    if (!faces[i])
                    {
                        return false;
                    }
                    D3D11_TEXTURE2D_DESC d{};
                    faces[i]->GetDesc(&d);
                    if (i == 0)
                    {
                        faceDesc = d;
                    }
                    if (d.ArraySize != 1 || d.Width != faceDesc.Width || d.Height != faceDesc.Height ||
                        d.Format != faceDesc.Format || d.MipLevels != faceDesc.MipLevels)
                    {
                        NS_LOG_ERROR(Graphics,
                                     "D3DTexture: 立方体の面 {} の大きさ・書式・段数が 1 枚目と揃わない ({}x{}, fmt={}, mip={})",
                                     facePaths[i],
                                     d.Width,
                                     d.Height,
                                     static_cast<int>(d.Format),
                                     d.MipLevels);
                        return false;
                    }
                }

                D3D11_TEXTURE2D_DESC cubeDesc{};
                cubeDesc.Width = faceDesc.Width;
                cubeDesc.Height = faceDesc.Height;
                cubeDesc.MipLevels = faceDesc.MipLevels;
                cubeDesc.ArraySize = 6;
                cubeDesc.Format = faceDesc.Format;
                cubeDesc.SampleDesc.Count = 1;
                cubeDesc.Usage = D3D11_USAGE_DEFAULT;
                cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

                ComPtr<ID3D11Texture2D> cube;
                const HRESULT hr = device->CreateTexture2D(&cubeDesc, nullptr, cube.GetAddressOf());
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(
                        Graphics, "D3DTexture: 立方体の CreateTexture2D 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }
                for (UINT face = 0; face < 6; ++face)
                {
                    for (UINT mip = 0; mip < cubeDesc.MipLevels; ++mip)
                    {
                        context->CopySubresourceRegion(cube.Get(),
                                                       D3D11CalcSubresource(mip, face, cubeDesc.MipLevels),
                                                       0,
                                                       0,
                                                       0,
                                                       faces[face].Get(),
                                                       mip,
                                                       nullptr);
                    }
                }
                if (!CreateCubeSrv(device, cube.Get(), outSrv))
                {
                    return false;
                }
                outResource = cube;
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
            if (!desc.path.empty())
            {
                if (!LoadImageFile(device, context, desc.path, resource, m_srv))
                {
                    resource.Reset();
                }
            }
            AdoptLoaded(device, std::move(resource), false);
        }

        std::unique_ptr<D3DTexture> D3DTexture::CreateCubeFromDds(const std::string& path)
        {
            std::unique_ptr<D3DTexture> texture(new D3DTexture());
            ID3D11Device* device = Gpu().device;
            ID3D11DeviceContext* context = Gpu().context;
            if (device == nullptr || context == nullptr)
            {
                return texture;
            }
            ComPtr<ID3D11Resource> resource;
            if (LoadImageFile(device, context, path, resource, texture->m_srv))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                texture->m_srv->GetDesc(&srvDesc);
                if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBE)
                {
                    NS_LOG_ERROR(Graphics, "D3DTexture: 立方体でない DDS は立方体のテクスチャにできない: {}", path);
                    resource.Reset();
                }
            }
            texture->AdoptLoaded(device, std::move(resource), true);
            return texture;
        }

        std::unique_ptr<D3DTexture> D3DTexture::CreateCubeFromFaces(const std::array<std::string, 6>& facePaths)
        {
            std::unique_ptr<D3DTexture> texture(new D3DTexture());
            ID3D11Device* device = Gpu().device;
            ID3D11DeviceContext* context = Gpu().context;
            if (device == nullptr || context == nullptr)
            {
                return texture;
            }
            ComPtr<ID3D11Resource> resource;
            if (!LoadCubeFaces(device, context, facePaths, resource, texture->m_srv))
            {
                resource.Reset();
            }
            texture->AdoptLoaded(device, std::move(resource), true);
            return texture;
        }

        std::unique_ptr<D3DTexture> D3DTexture::CreateCubeFallback()
        {
            std::unique_ptr<D3DTexture> texture(new D3DTexture());
            texture->AdoptLoaded(Gpu().device, nullptr, true);
            return texture;
        }

        void D3DTexture::AdoptLoaded(ID3D11Device* device, ComPtr<ID3D11Resource> loaded, bool cube) noexcept
        {
            if (loaded)
            {
                m_tex = AsTexture2D(loaded.Get());
                m_size = Texture2DSize(m_tex.Get());
                return;
            }
            // 読みかけで作ったビューを捨て、代替画像のビューだけを持つ
            m_srv.Reset();
            if (!CreateMagentaFallback(device, cube, m_tex, m_srv))
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

        bool D3DTexture::IsValid() const noexcept { return m_srv || m_rtv || m_dsv; }
        Size2D D3DTexture::Size() const noexcept { return m_size; }
        bool D3DTexture::IsUsingFallback() const noexcept { return m_fallback; }
        ID3D11Texture2D* D3DTexture::Native() const noexcept { return m_tex.Get(); }
        ID3D11ShaderResourceView* D3DTexture::Srv() const noexcept { return m_srv.Get(); }
        ID3D11RenderTargetView* D3DTexture::Rtv() const noexcept { return m_rtv.Get(); }
        ID3D11DepthStencilView* D3DTexture::Dsv() const noexcept { return m_dsv.Get(); }

        ComPtr<ID3D11Texture2D> ViewTexture2D(ID3D11View* view, D3D11_TEXTURE2D_DESC* outDesc) noexcept
        {
            if (view == nullptr)
            {
                return nullptr;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            if (resource == nullptr || FAILED(resource.As(&texture)))
            {
                return nullptr;
            }
            if (outDesc != nullptr)
            {
                texture->GetDesc(outDesc);
            }
            return texture;
        }
    } // namespace Gfx
} // namespace NS
