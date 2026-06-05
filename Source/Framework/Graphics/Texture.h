#pragma once

/// @file Texture.h
/// @brief NS::Graphics::Texture — 2D テクスチャ (DDS / WIC ロード対応)
///
/// @details 拡張子 .dds → DirectXTK DDSTextureLoader、 それ以外 → WICTextureLoader 経由
/// File I/O は `NS::Core::FileSystem` 経由なので将来 pak / VFS で透過対応可能
/// 読込失敗時は 1x1 マゼンタ fallback SRV が生成され、 `IsUsingFallback()` が true
/// 依存: 生成に Renderer の Device / Context を使う (context は保持しない、 バインドは Renderer 経由)

#include <filesystem>
#include <memory>

#include <Framework/Graphics/Buffer.h>
#include <Framework/Math/Math.h>

struct ID3D11ShaderResourceView;
struct ID3D11DeviceContext;

namespace NS::Graphics
{

    class Renderer;
    class Texture;

    namespace detail
    {
        /// Texture 内部の SRV を取得 (Material が PSSetShaderResources 等に使用)
        [[nodiscard]] ID3D11ShaderResourceView* GetSrv(Texture& texture) noexcept;

        /// Texture の SRV を context の slot + ステージ (VS/PS/GS) にバインドする
        /// 無効な texture または context==nullptr は no-op。 Renderer::BindTexture が本関数を呼ぶ
        void BindTexture(ID3D11DeviceContext* context,
                         const Texture& texture,
                         unsigned slot,
                         ShaderStage stages) noexcept;
    } // namespace detail

    /// Texture 構築パラメータ
    /// path が空 or 読込失敗時は 1x1 マゼンタ fallback が生成され、IsUsingFallback() が true になる
    struct TextureDesc
    {
        std::filesystem::path path;
        bool generateMipmaps = true;
        bool sRGB = false;
    };

    /// 2D テクスチャ。Cubemap / 3D Volume は対象外
    /// 拡張子 .dds → DirectXTK DDSTextureLoader、それ以外 → WICTextureLoader 経由
    /// File I/O は NS::Core::FileSystem 経由なので将来 pak / VFS で透過対応可能
    /// 依存: 生成に Renderer の Device / Context を使う。 context は保持せず、 バインドは Renderer::BindTexture 経由
    class Texture
    {
    public:
        struct Impl;

        Texture(Renderer& renderer, const TextureDesc& desc);
        Texture(Renderer& renderer, const std::filesystem::path& path);
        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&&) = delete;
        Texture& operator=(Texture&&) = delete;

        /// SRV が有効か。fallback でも true (1x1 マゼンタ SRV が必ず生成される)
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] NS::Math::Size2D Size() const noexcept;

        /// 読込失敗で fallback (1x1 マゼンタ) になっているかを問い合わせる
        /// デバッグ時のアセット欠落検知に使用
        [[nodiscard]] bool IsUsingFallback() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11ShaderResourceView* detail::GetSrv(Texture& texture) noexcept;
        friend void detail::BindTexture(ID3D11DeviceContext* context,
                                        const Texture& texture,
                                        unsigned slot,
                                        ShaderStage stages) noexcept;
    };

} // namespace NS::Graphics
