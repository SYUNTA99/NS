#pragma once

/// @file Material.h
/// @brief NS::Graphics::Material — Shader + Texture スロット + 内蔵 CB
///
/// @details Shader は共有参照 (非所有)、 Material 寿命中 shader が
/// 有効であること。 Sampler は s0 LinearWrap 固定、 複数 sampler
/// は将来拡張。 `Bind(Renderer&)` は shader / textures / CB / sampler を一括設定する
/// GPU バインドは渡された Renderer 経由で行い、 Material は DeviceContext を保持しない

#include "Framework/Core/NonCopyable.h"
#include "Framework/Graphics/Pipeline.h"

#include <cstddef>
#include <map>
#include <memory>

namespace NS::Graphics
{

    class Renderer;
    class Shader;
    class Texture;
    class Mesh;
    class Buffer;

    /// Material 構築パラメータ。shader 2 本は共有参照 (非所有)、Material 寿命中有効であること
    /// constantBufferSize=0 なら内部 CB は構築されず SetParams は何もしない
    struct MaterialDesc
    {
        /// 共有 頂点 Shader (.vs)。nullptr で IsValid()=false
        Shader* vertexShader = nullptr;
        /// 共有 ピクセル Shader (.ps)。nullptr で IsValid()=false
        Shader* pixelShader = nullptr;
        /// 内蔵 ConstantBuffer のバイト数 (alignas(16) + sizeof%16==0 必須)
        std::size_t constantBufferSize = 0;
        /// ConstantBuffer Bind 先スロット (Bind 先ステージは VS + PS 固定)
        unsigned cbSlot = 1;
        /// 描画バケット判定に使うブレンド方式。Alpha / Additive で半透明バケットへ分類される
        BlendMode blend = BlendMode::Opaque;
        /// 半透明ソートのタイブレーク。距離同値時に小さいほど先に描かれる
        int renderPriority = 0;
    };

    /// Shader* (非所有) + Texture スロット + 内蔵 ConstantBuffer を束ねる汎用 Material
    /// Sampler は s0 LinearWrap 固定。GPU バインドは Bind / SetParams に渡す Renderer 経由
    class Material : public NS::Core::NonCopyable
    {
    public:
        /// MaterialDesc から Material を生成する。 失敗時も非 null (IsValid() で検知)
        [[nodiscard]] static std::unique_ptr<Material> Create(const MaterialDesc& desc);

        ~Material();

        /// Shader が非 null で内部リソース構築済なら true。fallback shader でも true
        [[nodiscard]] bool IsValid() const noexcept;

        /// Shader::IsUsingFallback() への薄いラッパ (Material 独自の fallback 状態は持たない)
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// 構築時に宣言したブレンド方式。SceneBase の bucket 分類と Draw 時の Pipeline 選択に使う
        [[nodiscard]] BlendMode Blend() const noexcept;
        /// 構築時に宣言した半透明ソートのタイブレーク優先度
        [[nodiscard]] int RenderPriority() const noexcept;

        /// 指定スロットにテクスチャを割り当てる。texture=nullptr で割当解除と同義
        void SetTexture(unsigned slot, const Texture* texture) noexcept;
        /// 指定スロットの割り当てを解除する
        void ClearTexture(unsigned slot) noexcept;

        /// CB 更新。`alignas(16)` + `sizeof(T) % 16 == 0` 必須
        /// constantBufferSize=0 で構築された Material では何もしない
        template <typename T> void SetParams(Renderer& renderer, const T& params) noexcept
        {
            static_assert((sizeof(T) % 16) == 0,
                          "Material::SetParams<T> は sizeof(T) が 16 byte 倍数で alignas(16) 必須");
            static_assert(alignof(T) >= 16, "Material::SetParams<T> は struct alignas(16) 必須 (Vector3 16-byte 境界)");
            UpdateParamsRaw(renderer, &params, sizeof(T));
        }

        /// VS/PS bind → 全 Texture bind(slot, Pixel) → ConstantBuffer bind(cbSlot, VS + PS)
        /// → PSSetSamplers(0, LinearWrap) を renderer 経由で一括実行。Mesh::Draw の前に呼ぶ
        void Bind(Renderer& renderer) noexcept;

        /// 自分の Shader の VS バイトコードで mesh の InputLayout を生成する (mesh.CreateInputLayout への薄い委譲)
        /// 直接描画される mesh に対し描画前に呼ぶ。 冪等なので毎フレーム呼んでも安全
        void CreateInputLayoutFor(Mesh& mesh) noexcept;

    private:
        explicit Material(const MaterialDesc& desc);

        void UpdateParamsRaw(Renderer& renderer, const void* data, std::size_t bytes) noexcept;

        Shader* m_vertexShader = nullptr;
        Shader* m_pixelShader = nullptr;
        std::map<unsigned, const Texture*> m_textures;
        std::unique_ptr<Buffer> m_cb;
        unsigned m_cbSlot = 1;
        BlendMode m_blend = BlendMode::Opaque;
        int m_renderPriority = 0;
        bool m_valid = false;
    };

} // namespace NS::Graphics
