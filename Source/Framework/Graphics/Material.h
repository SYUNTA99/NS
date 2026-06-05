#pragma once

/// @file Material.h
/// @brief NS::Graphics::Material — Shader + Texture スロット + 内蔵 CB
///
/// @details Shader は共有参照 (非所有)、 Material 寿命中 shader が
/// 有効であること。 Sampler は s0 LinearWrap 固定、 複数 sampler
/// は将来拡張。 `Bind()` は shader / textures / CB / sampler を一括設定する
/// 依存: Renderer の DeviceContext を内部で保持するため Renderer より先に破棄すること

#include "Framework/Graphics/Buffer.h"

#include <cstddef>
#include <memory>

namespace NS::Graphics
{

    class Renderer;
    class Shader;
    class Texture;

    /// Material 構築パラメータ
    /// shader は共有参照、Material は所有しない。Material 寿命中 shader が有効であること
    /// constantBufferSize=0 のとき内部 ConstantBuffer は構築されず、SetParams は no-op になる
    struct MaterialDesc
    {
        /// 共有 Shader。nullptr で IsValid()=false
        Shader* shader = nullptr;
        /// 内蔵 ConstantBuffer のバイト数 (alignas(16) + sizeof%16==0 必須)
        std::size_t constantBufferSize = 0;
        /// ConstantBuffer Bind 先スロット
        unsigned cbSlot = 1;
        /// ConstantBuffer Bind 対象ステージ (既定: VS + PS)
        ShaderStage cbStages = ShaderStage::Vertex | ShaderStage::Pixel;
    };

    /// Generic 単一クラス Material
    /// Shader* (非所有) + Texture スロット (unsigned 番号) + 内蔵 ConstantBuffer
    /// Sampler は s0 LinearWrap 固定、複数 sampler は将来拡張
    /// 依存: Renderer の DeviceContext を内部で保持するため、Renderer より先に破棄すること
    class Material
    {
    public:
        struct Impl;

        Material(Renderer& renderer, const MaterialDesc& desc);
        ~Material();

        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;
        Material(Material&&) = delete;
        Material& operator=(Material&&) = delete;

        /// Shader が非 null で内部リソース構築済なら true。fallback shader でも true
        [[nodiscard]] bool IsValid() const noexcept;

        /// 共有 Shader が fallback 描画 (magenta) に切替わっているかを問い合わせる
        /// 内部の `Shader::IsUsingFallback()` への薄いラッパ、 Material 単体では独自の
        /// fallback 状態は持たない。 デバッグ時のシェーダ欠落検知に使用
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// 指定スロットにテクスチャを割り当てる。texture=nullptr で割当解除と同義
        void SetTexture(unsigned slot, const Texture* texture) noexcept;
        /// 指定スロットの割り当てを解除する
        void ClearTexture(unsigned slot) noexcept;

        /// CB 更新。`alignas(16)` + `sizeof(T) % 16 == 0` 必須
        /// constantBufferSize=0 で構築された Material では no-op
        template <typename T> void SetParams(const T& params) noexcept
        {
            static_assert((sizeof(T) % 16) == 0,
                          "Material::SetParams<T> は sizeof(T) が 16 byte 倍数で alignas(16) 必須");
            static_assert(alignof(T) >= 16, "Material::SetParams<T> は struct alignas(16) 必須 (Vector3 16-byte 境界)");
            UpdateParamsRaw(&params, sizeof(T));
        }

        /// shader->Bind() → 全 Texture::Bind(slot, Pixel) → ConstantBuffer::Bind(cbSlot, cbStages)
        /// → PSSetSamplers(0, LinearWrap) 一括実行。Mesh::Draw() の前に呼ぶ
        void Bind() noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        void UpdateParamsRaw(const void* data, std::size_t bytes) noexcept;
    };

} // namespace NS::Graphics
