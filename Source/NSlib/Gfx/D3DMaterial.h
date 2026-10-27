#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/D3DPipeline.h"

#include <cstddef>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class D3DDevice;
        class D3DShader;
        class D3DTexture;
        class D3DMesh;
        class D3DBuffer;

        //! マテリアルの初期構築用パラメータ
        //! 2つのシェーダーは非所有の参照として扱うため、本マテリアルの寿命中は有効に保つ必要がある
        //! 定数バッファサイズを0に指定した場合、内部バッファは構築されずパラメータ更新関数は機能しない
        struct D3DMaterialDesc
        {
            D3DShader* vertexShader = nullptr;
            D3DShader* pixelShader = nullptr;
            std::size_t constantBufferSize = 0;  // 内蔵定数バッファのサイズ。k_DrawConstantsSlot へ差す
            //! 材質の値のバッファ (b2) の大きさ。0 なら作らない。中身は SetMaterialParams で書く
            std::size_t materialParamsSize = 0;
            BlendMode blend = BlendMode::Opaque; // 描画順序の分類基準
            int renderPriority = 0;              // 半透明描画時のソート優先度
        };

        //! シェーダーとテクスチャ、定数バッファを組み合わせるマテリアル
        //! シェーダーは非所有で、マテリアルより長く生きること。サンプラは線形ラップ固定
        class D3DMaterial : public NonCopyable
        {
        public:
            [[nodiscard]] static std::unique_ptr<D3DMaterial> Create(const D3DMaterialDesc& desc);

            ~D3DMaterial();

            [[nodiscard]] bool IsValid() const noexcept;

            //! フォールバックの代替シェーダーが使用されているか判定する
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            //! 構築時に指定されたブレンド方式を返す
            [[nodiscard]] BlendMode Blend() const noexcept;
            //! 構築時に指定された半透明ソート時の優先度を返す
            [[nodiscard]] int RenderPriority() const noexcept;

            //! テクスチャを設定する。nullptr ならその枠を外す
            void SetTexture(uint slot, const D3DTexture* texture) noexcept;

            //! 定数バッファのパラメータを更新する
            //! 定数バッファを持たない D3DMaterial では何もしない
            //! @note T は alignas(16) で、サイズも 16 の倍数であること
            template <typename T> void SetParams(D3DDevice& device, const T& params) noexcept
            {
                static_assert((sizeof(T) % 16) == 0,
                              "D3DMaterial::SetParams<T> は sizeof(T) が 16 byte 倍数で alignas(16) 必須");
                static_assert(alignof(T) >= 16,
                              "D3DMaterial::SetParams<T> は struct alignas(16) 必須 (Vector3 16-byte 境界)");
                UpdateParamsRaw(device, &params, sizeof(T));
            }

            //! 材質の値のバッファ (b2) の中身を bytes にする
            //! 写して持ち、次の Bind で GPU へ送る。バッファを持たない時と、
            //! bytes の大きさがバッファと違う時は受けない
            void SetMaterialParams(std::span<const std::byte> bytes) noexcept;

            //! 描画の直前にシェーダー・テクスチャ・定数バッファ・サンプラを設定する
            void Bind(D3DDevice& device) noexcept;

            //! 保持している頂点シェーダーを利用して、対象メッシュの入力レイアウトを生成する
            //! @note 描画対象のメッシュに対して事前に呼び出す必要がある
            void CreateInputLayoutFor(D3DMesh& mesh) noexcept;

        private:
            explicit D3DMaterial(const D3DMaterialDesc& desc);

            void UpdateParamsRaw(D3DDevice& device, const void* data, std::size_t bytes) noexcept;

            D3DShader* m_vertexShader = nullptr; // 非所有の参照
            D3DShader* m_pixelShader = nullptr;  // 非所有の参照
            std::map<uint, const D3DTexture*> m_textures;
            std::unique_ptr<D3DBuffer> m_cb;
            std::unique_ptr<D3DBuffer> m_materialParams;
            std::vector<std::byte> m_materialParamBytes;
            bool m_materialParamsDirty = false;
            BlendMode m_blend = BlendMode::Opaque;
            int m_renderPriority = 0;
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS
