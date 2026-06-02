#include "Framework/Graphics/TextureArray.h"

#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace NS::Graphics
{
    using detail::ComPtr;

    struct TextureArray::Impl
    {
        ComPtr<ID3D11Texture2D> arrayTexture;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        std::uint16_t sliceCount = 0;
        bool fallback = false;
    };

    namespace
    {
        constexpr std::uint8_t kMagentaPixel[4] = {0xFF, 0x00, 0xFF, 0xFF};

        [[nodiscard]] bool IsDdsExtension(const std::filesystem::path& path)
        {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext == ".dds";
        }

        // 1 slice ぶん (1 枚の 2D texture) を WIC / DDS で staging texture として読み、
        // ArraySize=1 / mip=1 / R8G8B8A8 のサイズとコピー元の bytes をホスト側に持ち帰る
        // 失敗時は false で抜けて呼出側に slice skip を選ばせる
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
                NS_LOG_WARN(::NS::Core::LogCat::Graphics, "TextureArray slice load failed: {}", path.string());
                return false;
            }
            const auto& bytes = bytesOpt.value();
            const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());

            // STAGING usage は SHADER_RESOURCE bind 不可なので SRV 出力は要求しない (要求すると E_INVALIDARG)
            if (IsDdsExtension(path))
            {
                const HRESULT hr = DirectX::CreateDDSTextureFromMemory(
                    device, context, data, bytes.size(), outResource.GetAddressOf(), nullptr);
                if (FAILED(hr))
                {
                    NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                                "TextureArray slice load failed (DDS hr=0x{:08X}): {}",
                                static_cast<unsigned>(hr),
                                path.string());
                    return false;
                }
            }
            else
            {
                const DirectX::WIC_LOADER_FLAGS loadFlags =
                    sRGB ? DirectX::WIC_LOADER_FORCE_SRGB : DirectX::WIC_LOADER_IGNORE_SRGB;
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
                    NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                                "TextureArray slice load failed (WIC hr=0x{:08X}): {}",
                                static_cast<unsigned>(hr),
                                path.string());
                    return false;
                }
            }
            return true;
        }

        // 1x1 magenta の単一 slice fallback texture array を構築する
        // slicePaths が空 or 全 slice 失敗時に呼ばれ、 IsUsingFallback() == true を立てる
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
            srd.pSysMem = kMagentaPixel;
            srd.SysMemPitch = sizeof(kMagentaPixel);

            HRESULT hr = device->CreateTexture2D(&td, &srd, outTexture.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "TextureArray fallback CreateTexture2D 失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "TextureArray fallback CreateSRV 失敗 (hr=0x{:08X})",
                             static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }
    } // namespace

    TextureArray::TextureArray(Renderer& renderer, const TextureArrayDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        auto* device = detail::GetDevice(renderer);
        auto* context = detail::GetContext(renderer);
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "TextureArray: Renderer の Device / Context が無効");
            return;
        }
        m_pImpl->device = device;
        m_pImpl->context = context;

        // slice 数を上限 (kTotalSlices) で clamp。 超過分は WARN を出して捨てる
        std::vector<std::filesystem::path> paths = desc.slicePaths;
        if (paths.size() > static_cast<std::size_t>(kTotalSlices))
        {
            NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                        "TextureArray: slice 数 {} は kTotalSlices={} を超過、 末尾を切り捨て",
                        paths.size(),
                        static_cast<unsigned>(kTotalSlices));
            paths.resize(kTotalSlices);
        }

        // 1 枚目を読んで size / format を確定させる。 失敗 or 0 枚なら fallback array に切替
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
                NS_LOG_WARN(::NS::Core::LogCat::Graphics, "TextureArray: slicePaths が空、 magenta fallback で構築");
            }
            else
            {
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "TextureArray: 全 {} slice の読込に失敗、 magenta fallback で構築",
                            paths.size());
            }
            if (CreateMagentaFallbackArray(device, m_pImpl->arrayTexture, m_pImpl->srv))
            {
                m_pImpl->sliceCount = 1;
                m_pImpl->fallback = true;
            }
            return;
        }

        ComPtr<ID3D11Texture2D> firstTex2d;
        if (FAILED(firstResource.As(&firstTex2d)) || !firstTex2d)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "TextureArray: 1 枚目 slice の Texture2D QI 失敗");
            if (CreateMagentaFallbackArray(device, m_pImpl->arrayTexture, m_pImpl->srv))
            {
                m_pImpl->sliceCount = 1;
                m_pImpl->fallback = true;
            }
            return;
        }
        D3D11_TEXTURE2D_DESC firstDesc{};
        firstTex2d->GetDesc(&firstDesc);

        const UINT sliceWidth = firstDesc.Width;
        const UINT sliceHeight = firstDesc.Height;
        const DXGI_FORMAT sliceFormat = firstDesc.Format;

        // mipmap を GenerateMips で作るには RENDER_TARGET + GENERATE_MIPS が要る
        // 1024x1024 の log2 = 10 + 1 = 11 levels。 0 を渡すと自動算出
        const UINT mipLevels = desc.generateMipmaps ? 0u : 1u;

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
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "TextureArray: CreateTexture2D 失敗 (W={} H={} N={} hr=0x{:08X})",
                         sliceWidth,
                         sliceHeight,
                         static_cast<unsigned>(paths.size()),
                         static_cast<unsigned>(hr));
            if (CreateMagentaFallbackArray(device, m_pImpl->arrayTexture, m_pImpl->srv))
            {
                m_pImpl->sliceCount = 1;
                m_pImpl->fallback = true;
            }
            return;
        }

        // 各 slice 用 staging texture を順に読み、 CopySubresourceRegion で array へ転写する
        // 失敗 slice は magenta で埋める運用にせず、 後段の GenerateMips に渡る前にスキップして
        // 「失敗 1 枚以上 → fallback フラグ true、 ただし array 自体は機能継続」 とする
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
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "TextureArray: slice {} の Texture2D QI 失敗、 magenta 領域として残す",
                            i);
                anySliceFailed = true;
                continue;
            }
            D3D11_TEXTURE2D_DESC sd{};
            sliceTex2d->GetDesc(&sd);
            if (sd.Width != sliceWidth || sd.Height != sliceHeight || sd.Format != sliceFormat)
            {
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "TextureArray: slice {} のサイズ / フォーマットが基準と不一致 (skip): {}",
                            i,
                            paths[i].string());
                anySliceFailed = true;
                continue;
            }

            // 配列 slice = mip 0 のサブリソース番号 = arraySlice * MipLevelsActual + mip
            // GenerateMips 待ちの状態では実 MipLevels は arrayDesc.MipLevels と一致する
            ComPtr<ID3D11Texture2D> arrayTex2d = arrayTexture;
            D3D11_TEXTURE2D_DESC actualArrayDesc{};
            arrayTex2d->GetDesc(&actualArrayDesc);
            const UINT mipsActual = actualArrayDesc.MipLevels;
            const UINT dstSub = D3D11CalcSubresource(0, static_cast<UINT>(i), mipsActual);
            context->CopySubresourceRegion(arrayTexture.Get(), dstSub, 0, 0, 0, sliceTex2d.Get(), 0, nullptr);
        }

        // SRV を Texture2DArray 視点で生成。 mip auto-gen の場合 MipLevels=-1 で全 level を露出する
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
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "TextureArray: CreateShaderResourceView 失敗 (hr=0x{:08X})",
                         static_cast<unsigned>(hr));
            if (CreateMagentaFallbackArray(device, m_pImpl->arrayTexture, m_pImpl->srv))
            {
                m_pImpl->sliceCount = 1;
                m_pImpl->fallback = true;
            }
            return;
        }

        if (desc.generateMipmaps)
        {
            context->GenerateMips(srv.Get());
        }

        m_pImpl->arrayTexture = arrayTexture;
        m_pImpl->srv = srv;
        m_pImpl->sliceCount = static_cast<std::uint16_t>(paths.size());
        m_pImpl->fallback = anySliceFailed;
    }

    TextureArray::~TextureArray() = default;

    bool TextureArray::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->srv;
    }

    bool TextureArray::IsUsingFallback() const noexcept
    {
        return m_pImpl && m_pImpl->fallback;
    }

    std::uint16_t TextureArray::SliceCount() const noexcept
    {
        return m_pImpl ? m_pImpl->sliceCount : static_cast<std::uint16_t>(0);
    }

    void TextureArray::Bind(unsigned slot, ShaderStage stages) const noexcept
    {
        if (!IsValid() || !m_pImpl->context)
        {
            return;
        }
        ID3D11ShaderResourceView* srvs[1] = {m_pImpl->srv.Get()};
        if (HasStage(stages, ShaderStage::Vertex))
        {
            m_pImpl->context->VSSetShaderResources(slot, 1u, srvs);
        }
        if (HasStage(stages, ShaderStage::Pixel))
        {
            m_pImpl->context->PSSetShaderResources(slot, 1u, srvs);
        }
        if (HasStage(stages, ShaderStage::Geometry))
        {
            m_pImpl->context->GSSetShaderResources(slot, 1u, srvs);
        }
    }

    namespace detail
    {
        ID3D11ShaderResourceView* GetSrv(TextureArray& textureArray) noexcept
        {
            return textureArray.m_pImpl ? textureArray.m_pImpl->srv.Get() : nullptr;
        }
    } // namespace detail

} // namespace NS::Graphics
