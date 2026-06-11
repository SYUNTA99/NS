#pragma once

/// @file TextureArray.h
/// @brief NS::Graphics::TextureArray — block 用 Texture2DArray ラッパ
///
/// @details `ID3D11Texture2D` の ArraySize=N で N 枚の 2D テクスチャを 1 リソースに集約し、
/// HLSL からは `Texture2DArray::Sample(samp, float3(uv, sliceIndex))` の 3 成分目で
/// slice を選ぶ。 全 slice は同一 width / height / format / mip count 必須 (D3D11 仕様)
/// NS では 1024x1024 BC1 + mipmap を標準とするが、 WIC ロード時は内部で同一サイズに
/// 正規化される (アセット側で揃える運用)
/// 読込失敗時は 1x1 magenta fallback を slice 0 に詰め、 `IsUsingFallback()` が true に
/// なる (Texture と同じ流派)
/// バインドは CommandList 経由 (本型は context を保持しない)
/// D3D11 型を公開する設計のため `ID3D11Texture2D*` / SRV を直接公開する

#include <Framework/Core/NonCopyable.h>
#include <Framework/Graphics/Buffer.h>
#include <Framework/Graphics/D3dCommon.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace NS::Graphics
{

    class TextureArray;

    /// TextureArray 構築パラメータ。`slicePaths` の先頭から slice 0, 1, ... を埋める (0 枚なら fallback)
    /// 上限は `TextureArray::kTotalSlices` (64)、 超過分は捨てて WARN を出す
    struct TextureArrayDesc
    {
        std::vector<std::filesystem::path> slicePaths;
        bool generateMipmaps = true;
        bool sRGB = false;
    };

    /// 1 つの `ID3D11Texture2D` (ArraySize=N) を保有する block 描画専用ラッパ。バインドは CommandList 経由
    /// 全 slice 同一 width / height / format / mip count が D3D11 仕様で必須
    class TextureArray : public NS::Core::NonCopyable
    {
    public:
        /// slice 予算上限。 5 theme x 8 variant = 40 を確保し、 24 slot を将来拡張用に残す
        /// 超過分はコンストラクタ内で捨てて WARN を出す (境界 clamp)
        static constexpr std::uint16_t kTotalSlices = 64;

        /// TextureArrayDesc から Texture2DArray を生成する。 失敗時も非 null (fallback / IsUsingFallback())
        [[nodiscard]] static std::unique_ptr<TextureArray> Create(const TextureArrayDesc& desc);

        ~TextureArray();

        /// SRV が有効なら true。 fallback でも true (1x1 magenta が必ず生成される)
        [[nodiscard]] bool IsValid() const noexcept;

        /// 任意の slice 読込失敗 or `slicePaths` が空で fallback (1x1 magenta) になっているかを問い合わせる
        /// デバッグ時のアセット欠落検知に使用 (Texture::IsUsingFallback と同じ思想)
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// 実際に確保された slice 数。 `desc.slicePaths.size()` を `kTotalSlices` で clamp した値
        /// fallback 経路では 1 (magenta slice のみ) を返す
        [[nodiscard]] std::uint16_t SliceCount() const noexcept;

        /// 内部 ID3D11Texture2D (ArraySize=N)。継ぎ目で生 D3D を扱う場合に使う
        [[nodiscard]] ID3D11Texture2D* Native() const noexcept;

        /// Texture2DArray 視点の SRV (fallback でも非 null)
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;

    private:
        explicit TextureArray(const TextureArrayDesc& desc);

        ComPtr<ID3D11Texture2D> m_arrayTexture;
        ComPtr<ID3D11ShaderResourceView> m_srv;
        std::uint16_t m_sliceCount = 0;
        bool m_fallback = false;
    };

} // namespace NS::Graphics
