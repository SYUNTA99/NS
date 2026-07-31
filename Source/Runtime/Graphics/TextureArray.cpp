#include "Runtime/Graphics/TextureArray.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/GraphicObject.h"

#include <algorithm>
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>

namespace NS::Graphics
{

    namespace
    {
        constexpr std::uint8_t k_MagentaPixel[4] = {0xFF, 0x00, 0xFF, 0xFF};

        [[nodiscard]] bool IsDdsExtension(const std::filesystem::path& path)
        {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext == ".dds";
        }

        // 1枚の画像を読み込む。失敗した場合はスキップして処理を続行する
        [[nodiscard]] bool TryLoadSliceResource(ID3D11Device* device,
                                                ID3D11DeviceContext* context,
                                                const std::filesystem::path& path,
                                                bool sRGB,
                                                ComPtr<ID3D11Resource>& outResource) noexcept
        {
            if (path.empty())
            {
                return false;
            }
            auto bytesOpt = ::NS::Core::FileSystem::ReadAllBytes(path);
            if (!bytesOpt.has_value())
            {
                NS_LOG_WARN(Graphics, "TextureArray slice load failed: {}", path.string());
                return false;
            }
            const auto& bytes = bytesOpt.value();
            const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());

            // 読み出し専用として構築するため、シェーダ用のビューは要求しない
            if (IsDdsExtension(path))
            {
                const HRESULT hr = DirectX::CreateDDSTextureFromMemory(
                    device, context, data, bytes.size(), outResource.GetAddressOf(), nullptr);
                if (FAILED(hr))
                {
                    NS_LOG_WARN(Graphics,
                                "TextureArray slice load failed (DDS hr=0x{:08X}): {}",
                                static_cast<unsigned>(hr),
                                path.string());
                    return false;
                }
            }
            else
            {
                const DirectX::WIC_LOADER_FLAGS loadFlags = [&]() -> DirectX::WIC_LOADER_FLAGS {
                    if (sRGB)
                        return DirectX::WIC_LOADER_FORCE_SRGB;
                    return DirectX::WIC_LOADER_IGNORE_SRGB;
                }();
                const HRESULT hr = DirectX::CreateWICTextureFromMemoryEx(device,
                                                                         nullptr,
                                                                         data,
                                                                         bytes.size(),
                                                                         0u,
                                                                         D3D11_USAGE_STAGING,
                                                                         0u,
                                                                         D3D11_CPU_ACCESS_READ,
                                                                         0u,
                                                                         loadFlags,
                                                                         outResource.GetAddressOf(),
                                                                         nullptr);
                if (FAILED(hr))
                {
                    NS_LOG_WARN(Graphics,
                                "TextureArray slice load failed (WIC hr=0x{:08X}): {}",
                                static_cast<unsigned>(hr),
                                path.string());
                    return false;
                }
            }
            return true;
        }

        // 読み込みに失敗した場合の代替画像（ピンク色）を生成する
        [[nodiscard]] bool CreateMagentaFallbackArray(ID3D11Device* device,
                                                      ComPtr<ID3D11Texture2D>& outTexture,
                                                      ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
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
            srd.pSysMem = k_MagentaPixel;
            srd.SysMemPitch = sizeof(k_MagentaPixel);

            HRESULT hr = device->CreateTexture2D(&td, &srd, outTexture.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    Graphics, "TextureArray fallback CreateTexture2D 失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
            svd.Format = td.Format;
            svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            svd.Texture2DArray.MostDetailedMip = 0;
            svd.Texture2DArray.MipLevels = 1;
            svd.Texture2DArray.FirstArraySlice = 0;
            svd.Texture2DArray.ArraySize = 1;

            hr = device->CreateShaderResourceView(outTexture.Get(), &svd, outSrv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "TextureArray fallback CreateSRV 失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }
    } // namespace

    std::unique_ptr<TextureArray> TextureArray::Create(const TextureArrayDesc& desc)
    {
        return std::unique_ptr<TextureArray>(new TextureArray(desc));
    }

    TextureArray::TextureArray(const TextureArrayDesc& desc)
    {
        auto* device = Gpu().device;
        auto* context = Gpu().context;
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(Graphics, "TextureArray: Renderer の Device / Context が無効");
            return;
        }

        std::vector<std::filesystem::path> paths = desc.slicePaths;
        if (paths.size() > static_cast<std::size_t>(k_TotalSlices))
        {
            NS_LOG_WARN(Graphics,
                        "TextureArray: slice 数 {} は k_TotalSlices={} を超過、 末尾を切り捨て",
                        paths.size(),
                        static_cast<unsigned>(k_TotalSlices));
            paths.resize(k_TotalSlices);
        }

        // 1枚目の画像を読み込み、テクスチャ配列全体の基準となるサイズとフォーマットを決定する
        ComPtr<ID3D11Resource> firstResource;
        bool firstLoaded = false;
        std::size_t firstLoadedIndex = 0;
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            firstResource.Reset();
            if (TryLoadSliceResource(device, context, paths[i], desc.sRGB, firstResource))
            {
                firstLoaded = true;
                firstLoadedIndex = i;
                break;
            }
        }

        if (!firstLoaded)
        {
            if (paths.empty())
            {
                NS_LOG_WARN(Graphics, "TextureArray: slicePaths が空、 magenta fallback で構築");
            }
            else
            {
                NS_LOG_WARN(Graphics, "TextureArray: 全 {} slice の読込に失敗、 magenta fallback で構築", paths.size());
            }
            if (CreateMagentaFallbackArray(device, m_arrayTexture, m_srv))
            {
                m_sliceCount = 1;
                m_fallback = true;
            }
            return;
        }

        ComPtr<ID3D11Texture2D> firstTex2d;
        if (FAILED(firstResource.As(&firstTex2d)) || !firstTex2d)
        {
            NS_LOG_ERROR(Graphics, "TextureArray: 1 枚目 slice の Texture2D QI 失敗");
            if (CreateMagentaFallbackArray(device, m_arrayTexture, m_srv))
            {
                m_sliceCount = 1;
                m_fallback = true;
            }
            return;
        }
        D3D11_TEXTURE2D_DESC firstDesc{};
        firstTex2d->GetDesc(&firstDesc);

        const UINT sliceWidth = firstDesc.Width;
        const UINT sliceHeight = firstDesc.Height;
        const DXGI_FORMAT sliceFormat = firstDesc.Format;

        // ミップマップを自動生成するかどうかに応じて設定を切り替える（0を指定すると最大数まで自動算出される）
        const UINT mipLevels = [&]() -> UINT {
            if (desc.generateMipmaps)
                return 0u;
            return 1u;
        }();

        D3D11_TEXTURE2D_DESC arrayDesc{};
        arrayDesc.Width = sliceWidth;
        arrayDesc.Height = sliceHeight;
        arrayDesc.MipLevels = mipLevels;
        arrayDesc.ArraySize = static_cast<UINT>(paths.size());
        arrayDesc.Format = sliceFormat;
        arrayDesc.SampleDesc.Count = 1;
        arrayDesc.SampleDesc.Quality = 0;
        arrayDesc.Usage = D3D11_USAGE_DEFAULT;
        arrayDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        arrayDesc.MiscFlags = 0;
        if (desc.generateMipmaps)
        {
            arrayDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
            arrayDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        }

        ComPtr<ID3D11Texture2D> arrayTexture;
        HRESULT hr = device->CreateTexture2D(&arrayDesc, nullptr, arrayTexture.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(Graphics,
                         "TextureArray: CreateTexture2D 失敗 (W={} H={} N={} hr=0x{:08X})",
                         sliceWidth,
                         sliceHeight,
                         static_cast<unsigned>(paths.size()),
                         static_cast<unsigned>(hr));
            if (CreateMagentaFallbackArray(device, m_arrayTexture, m_srv))
            {
                m_sliceCount = 1;
                m_fallback = true;
            }
            return;
        }

        // 読み込んだ各画像をテクスチャ配列に転送する。サイズの不一致や読み込みに失敗した画像はスキップする
        bool anySliceFailed = false;
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            ComPtr<ID3D11Resource> sliceResource;
            if (i == firstLoadedIndex)
            {
                sliceResource = firstResource;
            }
            else
            {
                if (!TryLoadSliceResource(device, context, paths[i], desc.sRGB, sliceResource))
                {
                    anySliceFailed = true;
                    continue;
                }
            }

            ComPtr<ID3D11Texture2D> sliceTex2d;
            if (FAILED(sliceResource.As(&sliceTex2d)) || !sliceTex2d)
            {
                NS_LOG_WARN(Graphics, "TextureArray: slice {} の Texture2D QI 失敗、 magenta 領域として残す", i);
                anySliceFailed = true;
                continue;
            }
            D3D11_TEXTURE2D_DESC sd{};
            sliceTex2d->GetDesc(&sd);
            if (sd.Width != sliceWidth || sd.Height != sliceHeight || sd.Format != sliceFormat)
            {
                NS_LOG_WARN(Graphics,
                            "TextureArray: slice {} のサイズ / フォーマットが基準と不一致 (skip): {}",
                            i,
                            paths[i].string());
                anySliceFailed = true;
                continue;
            }

            // 対象となるスライスの位置を計算し、画像データをコピーする
            ComPtr<ID3D11Texture2D> arrayTex2d = arrayTexture;
            D3D11_TEXTURE2D_DESC actualArrayDesc{};
            arrayTex2d->GetDesc(&actualArrayDesc);
            const UINT mipsActual = actualArrayDesc.MipLevels;
            const UINT dstSub = D3D11CalcSubresource(0, static_cast<UINT>(i), mipsActual);
            context->CopySubresourceRegion(arrayTexture.Get(), dstSub, 0, 0, 0, sliceTex2d.Get(), 0, nullptr);
        }

        // シェーダからテクスチャ配列全体を参照できるようにするためのビューを生成する
        D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
        svd.Format = sliceFormat;
        svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        svd.Texture2DArray.MostDetailedMip = 0;
        svd.Texture2DArray.MipLevels = static_cast<UINT>(-1);
        svd.Texture2DArray.FirstArraySlice = 0;
        svd.Texture2DArray.ArraySize = static_cast<UINT>(paths.size());

        ComPtr<ID3D11ShaderResourceView> srv;
        hr = device->CreateShaderResourceView(arrayTexture.Get(), &svd, srv.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(
                Graphics, "TextureArray: CreateShaderResourceView 失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
            if (CreateMagentaFallbackArray(device, m_arrayTexture, m_srv))
            {
                m_sliceCount = 1;
                m_fallback = true;
            }
            return;
        }

        if (desc.generateMipmaps)
        {
            context->GenerateMips(srv.Get());
        }

        m_arrayTexture = arrayTexture;
        m_srv = srv;
        m_sliceCount = static_cast<std::uint16_t>(paths.size());
        m_fallback = anySliceFailed;
    }

    TextureArray::~TextureArray() = default;

    bool TextureArray::IsValid() const noexcept
    {
        return m_srv != nullptr;
    }

    bool TextureArray::IsUsingFallback() const noexcept
    {
        return m_fallback;
    }

    std::uint16_t TextureArray::SliceCount() const noexcept
    {
        return m_sliceCount;
    }

    ID3D11Texture2D* TextureArray::Native() const noexcept
    {
        return m_arrayTexture.Get();
    }

    ID3D11ShaderResourceView* TextureArray::Srv() const noexcept
    {
        return m_srv.Get();
    }

} // namespace NS::Graphics