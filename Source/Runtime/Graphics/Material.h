#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Pipeline.h"

#include <map>
#include <memory>

namespace NS::Graphics
{

    class Renderer;
    class Shader;
    class Texture;
    class Mesh;
    class Buffer;

    //! @brief マテリアルの初期構築用パラメータ。
    //! @details 2つのシェーダーは非所有の参照として扱うため、本マテリアルの寿命中は有効に保つ必要がある。
    //! 定数バッファサイズを0に指定した場合、内部バッファは構築されずパラメータ更新関数は機能しない。
    struct MaterialDesc
    {
        Shader* vertexShader = nullptr;      //!< 頂点シェーダー
        Shader* pixelShader = nullptr;       //!< ピクセルシェーダー
        std::size_t constantBufferSize = 0;  //!< 内蔵定数バッファのサイズ
        unsigned cbSlot = 1;                 //!< 定数バッファのバインド先スロット
        BlendMode blend = BlendMode::Opaque; //!< 描画順序の分類基準
        int renderPriority = 0;              //!< 半透明描画時のソート優先度
    };

    //! @brief シェーダー、テクスチャ、および内蔵の定数バッファを管理する汎用マテリアル。
    //! @details シェーダーは非所有の参照として保持するため、本クラスの寿命が尽きるまで外部で有効に保つ必要がある。
    //! サンプラは線形ラップ設定に固定されている。描画システム経由でのバインド処理を想定する。
    class Material : public NS::Core::NonCopyable
    {
    public:
        //! マテリアルを生成する
        [[nodiscard]] static std::unique_ptr<Material> Create(const MaterialDesc& desc);

        ~Material();

        [[nodiscard]] bool IsValid() const noexcept;

        //! フォールバックの代替シェーダーが使用されているか判定する
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        //! 構築時に指定されたブレンド方式を返す
        [[nodiscard]] BlendMode Blend() const noexcept;
        //! 構築時に指定された半透明ソート時の優先度を返す
        [[nodiscard]] int RenderPriority() const noexcept;

        //! テクスチャを設定
        void SetTexture(unsigned slot, const Texture* texture) noexcept;
        //! テクスチャをクリア
        void ClearTexture(unsigned slot) noexcept;

        //! @brief 定数バッファのパラメータを更新する
        //! @details 初期化時に定数バッファサイズが0として構築された場合は何も行わない
        //! @note 型Tは16バイトアライメント（alignas(16)）および16の倍数サイズであることが必須となる
        template <typename T> void SetParams(Renderer& renderer, const T& params) noexcept
        {
            static_assert((sizeof(T) % 16) == 0,
                          "Material::SetParams<T> は sizeof(T) が 16 byte 倍数で alignas(16) 必須");
            static_assert(alignof(T) >= 16, "Material::SetParams<T> は struct alignas(16) 必須 (Vector3 16-byte 境界)");
            UpdateParamsRaw(renderer, &params, sizeof(T));
        }

        //! @brief シェーダー、テクスチャ、定数バッファ、サンプラなどの状態を描画システムに一括でバインドする
        //! @note 実際の描画処理の直前に呼び出すこと
        void Bind(Renderer& renderer) noexcept;

        //! @brief 保持している頂点シェーダーを利用して、対象メッシュの入力レイアウトを生成する
        //! @note 描画対象のメッシュに対して事前に呼び出す必要がある
        void CreateInputLayoutFor(Mesh& mesh) noexcept;

    private:
        explicit Material(const MaterialDesc& desc);

        void UpdateParamsRaw(Renderer& renderer, const void* data, std::size_t bytes) noexcept;

        Shader* m_vertexShader = nullptr; // 非所有の参照
        Shader* m_pixelShader = nullptr;  // 非所有の参照
        std::map<unsigned, const Texture*> m_textures;
        std::unique_ptr<Buffer> m_cb; // 内蔵の定数バッファ
        unsigned m_cbSlot = 1;
        BlendMode m_blend = BlendMode::Opaque;
        int m_renderPriority = 0;
        bool m_valid = false;
    };

} // namespace NS::Graphics
