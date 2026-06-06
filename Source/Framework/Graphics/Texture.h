#pragma once

/// @file Texture.h
/// @brief NS::Graphics::Texture — 2D テクスチャ (ファイルロード / 生成 / 既存リソースラップ兼用)
///
/// @details 役割は `TextureCreateDesc::bindFlags` (SHADER_RESOURCE / RENDER_TARGET / DEPTH_STENCIL の
/// 組合せ) で決まり、 必要な view (`Srv()` / `Rtv()` / `Dsv()`) だけを生成・保持する (用途外アクセサは null)
/// ファイルロードは拡張子 .dds → DDSTextureLoader、 それ以外 → WICTextureLoader 経由で、 読込失敗時は
/// 1x1 マゼンタ fallback SRV を生成し `IsUsingFallback()` が true になる。 File I/O は `NS::Core::FileSystem`
/// 経由なので将来 pak / VFS で透過対応可能
/// 既存 `ID3D11Texture2D` ラップ ctor は swapchain backbuffer を RTV として包む用途に使う
/// バインドは Renderer 経由 (`Renderer::BindTexture`)、 本型は context を保持しない
/// Graphics は exposed-D3D lean 設計のため `ID3D11Texture2D*` / 各 view を直接公開する

#include <filesystem>

#include <Framework/Graphics/Buffer.h>
#include <Framework/Math/Math.h>

#include <d3d11.h>
#include <wrl/client.h>

namespace NS::Graphics
{

    class Renderer;
    class Texture;

    namespace detail
    {
        /// Texture 内部の SRV を取得 (Material が PSSetShaderResources 等に使用)。 SRV を持たなければ null
        [[nodiscard]] ID3D11ShaderResourceView* GetSrv(Texture& texture) noexcept;
    } // namespace detail

    /// ファイルロード用 Texture 構築パラメータ
    /// path が空 or 読込失敗時は 1x1 マゼンタ fallback が生成され、 IsUsingFallback() が true になる
    struct TextureDesc
    {
        std::filesystem::path path;
        bool generateMipmaps = true;
        bool sRGB = false;
    };

    /// 生成用 Texture 構築パラメータ (offscreen RT / depth / render-to-texture)
    /// bindFlags に SHADER_RESOURCE / RENDER_TARGET / DEPTH_STENCIL を組み合わせ、 必要な view が作られる
    /// 例: RENDER_TARGET|SHADER_RESOURCE で render-to-texture (Rtv() と Srv() 両方が非 null)
    struct TextureCreateDesc
    {
        UINT width = 0;
        UINT height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT mipLevels = 1;
        UINT arraySize = 1;
        UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;
    };

    /// 2D テクスチャ。Cubemap / 3D Volume は対象外
    /// 役割は bindFlags で決まり、 SRV / RTV / DSV を必要なぶんだけ保持する
    /// バインドは Renderer::BindTexture 経由 (本型は context を保持しない)
    class Texture
    {
    public:
        Texture(Renderer& renderer, const TextureDesc& desc);
        Texture(Renderer& renderer, const std::filesystem::path& path);
        Texture(Renderer& renderer, const TextureCreateDesc& desc);
        Texture(Renderer& renderer, Microsoft::WRL::ComPtr<ID3D11Texture2D> existing, UINT bindFlags);
        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&&) = delete;
        Texture& operator=(Texture&&) = delete;

        /// いずれかの view (SRV/RTV/DSV) が有効なら true。fallback でも true (1x1 マゼンタ SRV が生成される)
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] NS::Math::Size2D Size() const noexcept;

        /// 読込失敗で fallback (1x1 マゼンタ) になっているかを問い合わせる
        /// デバッグ時のアセット欠落検知に使用。 生成 / ラップ ctor では常に false
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// 内部 ID3D11Texture2D。継ぎ目で raw D3D を扱う Renderer / detail が使う
        [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

        /// SHADER_RESOURCE bind 時の SRV (なければ null)
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;
        /// RENDER_TARGET bind 時の RTV (なければ null)
        [[nodiscard]] ID3D11RenderTargetView* Rtv() const noexcept;
        /// DEPTH_STENCIL bind 時の DSV (なければ null)
        [[nodiscard]] ID3D11DepthStencilView* Dsv() const noexcept;

    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;
        NS::Math::Size2D m_size{0, 0};
        bool m_fallback = false;
    };

} // namespace NS::Graphics
