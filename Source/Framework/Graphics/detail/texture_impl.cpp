#include "Framework/Graphics/Texture.h"

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

namespace NS::Graphics
{
    using detail::ComPtr;

    struct Texture::Impl
    {
        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11DeviceContext> context;
        ::NS::Core::Size2D size{0, 0};
        bool fallback = false;
    };

    namespace
    {
        [[nodiscard]] bool IsDdsExtension(const std::filesystem::path& path)
        {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext == ".dds";
        }

        bool QueryTexture2DSize(ID3D11Resource* resource, int& outWidth, int& outHeight) noexcept
        {
            if (resource == nullptr)
            {
                return false;
            }
            ComPtr<ID3D11Texture2D> tex2d;
            const HRESULT hr = resource->QueryInterface(IID_PPV_ARGS(tex2d.GetAddressOf()));
            if (FAILED(hr) || !tex2d)
            {
                return false;
            }
            D3D11_TEXTURE2D_DESC d{};
            tex2d->GetDesc(&d);
            outWidth = static_cast<int>(d.Width);
            outHeight = static_cast<int>(d.Height);
            return true;
        }

        bool CreateMagentaFallback(ID3D11Device* device,
                                   ComPtr<ID3D11Resource>& outResource,
                                   ComPtr<ID3D11ShaderResourceView>& outSrv,
                                   int& outWidth,
                                   int& outHeight) noexcept
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Texture fallback CreateTexture2D 失敗 (hr=0x{:X})",
                             static_cast<unsigned>(hr));
                return false;
            }

            hr = device->CreateShaderResourceView(tex2d.Get(), nullptr, outSrv.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Texture fallback CreateSRV 失敗 (hr=0x{:X})",
                             static_cast<unsigned>(hr));
                return false;
            }

            outResource = tex2d;
            outWidth = 1;
            outHeight = 1;
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
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "CreateDDSTextureFromMemory 失敗 (hr=0x{:X})",
                             static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        bool TryLoadWic(ID3D11Device* device,
                        ID3D11DeviceContext* context,
                        const std::uint8_t* bytes,
                        std::size_t size,
                        bool generateMipmaps,
                        bool sRGB,
                        ComPtr<ID3D11Resource>& outResource,
                        ComPtr<ID3D11ShaderResourceView>& outSrv) noexcept
        {
            const DirectX::WIC_LOADER_FLAGS loadFlags =
                sRGB ? DirectX::WIC_LOADER_FORCE_SRGB : DirectX::WIC_LOADER_IGNORE_SRGB;

            ID3D11DeviceContext* ctxForMipmap = generateMipmaps ? context : nullptr;

            const HRESULT hr = DirectX::CreateWICTextureFromMemoryEx(
                device,
                ctxForMipmap,
                bytes,
                size,
                0u,
                D3D11_USAGE_DEFAULT,
                D3D11_BIND_SHADER_RESOURCE | (generateMipmaps ? D3D11_BIND_RENDER_TARGET : 0u),
                0u,
                generateMipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0u,
                loadFlags,
                outResource.GetAddressOf(),
                outSrv.GetAddressOf());

            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "CreateWICTextureFromMemoryEx 失敗 (hr=0x{:X})",
                             static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }
    } // namespace

    Texture::Texture(Renderer& renderer, const TextureDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        auto* device = detail::GetDevice(renderer);
        auto* context = detail::GetContext(renderer);
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Texture: Renderer の Device / Context が無効");
            return;
        }
        m_pImpl->context = context;

        bool loaded = false;
        if (!desc.path.empty())
        {
            auto bytesOpt = ::NS::Core::FileSystem::ReadAllBytes(desc.path);
            if (bytesOpt.has_value())
            {
                const auto& bytes = bytesOpt.value();
                const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
                if (IsDdsExtension(desc.path))
                {
                    // DDS は内蔵 mipmap を尊重、generateMipmaps フラグは無視。
                    // mipmap が欲しければ Texconv.exe 等で事前生成した DDS を渡すこと。
                    loaded = TryLoadDds(device, context, data, bytes.size(), m_pImpl->resource, m_pImpl->srv);
                }
                else
                {
                    loaded = TryLoadWic(device,
                                        context,
                                        data,
                                        bytes.size(),
                                        desc.generateMipmaps,
                                        desc.sRGB,
                                        m_pImpl->resource,
                                        m_pImpl->srv);
                }
            }
            else
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Texture load failed: {}", desc.path.string());
            }
        }

        if (loaded)
        {
            int w = 0;
            int h = 0;
            if (QueryTexture2DSize(m_pImpl->resource.Get(), w, h))
            {
                m_pImpl->size = ::NS::Core::Size2D{w, h};
            }
            return;
        }

        int fbW = 0;
        int fbH = 0;
        if (!CreateMagentaFallback(device, m_pImpl->resource, m_pImpl->srv, fbW, fbH))
        {
            // fallback も失敗したら IsValid()==false、context だけ残らないよう統一クリアする。
            m_pImpl->context.Reset();
            return;
        }
        m_pImpl->size = ::NS::Core::Size2D{fbW, fbH};
        m_pImpl->fallback = true;
    }

    Texture::Texture(Renderer& renderer, const std::filesystem::path& path)
        : Texture(renderer, TextureDesc{path, true, false})
    {}

    Texture::~Texture() = default;

    bool Texture::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->srv;
    }
    ::NS::Core::Size2D Texture::Size() const noexcept
    {
        return m_pImpl ? m_pImpl->size : ::NS::Core::Size2D{0, 0};
    }
    bool Texture::IsUsingFallback() const noexcept
    {
        return m_pImpl && m_pImpl->fallback;
    }

    void Texture::Bind(unsigned slot, ShaderStage stages) const noexcept
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
        ID3D11ShaderResourceView* GetSrv(Texture& texture) noexcept
        {
            return texture.m_pImpl ? texture.m_pImpl->srv.Get() : nullptr;
        }
    } // namespace detail

} // namespace NS::Graphics
