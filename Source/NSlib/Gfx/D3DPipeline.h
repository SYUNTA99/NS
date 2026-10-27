#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <memory>

namespace NS
{
    namespace Gfx
    {
        /// ポリゴンのどちらの面を描画せずにカリングするか
        enum class CullMode
        {
            None,
            Back,
            Front
        };

        enum class FillMode
        {
            Solid,
            Wireframe
        };

        /// 描画する色と背景色をどのように合成するか
        enum class BlendMode
        {
            Opaque,
            Alpha,
            Additive
        };

        /// 深度の読み書き設定
        /// ReadOnly は手前かどうかを判定するが深度を書かない。半透明と背景に使う。
        /// Occluded は手前の物に隠れた画素だけを描き、深度を書かない。隠れた所を透かして見せる時に使う
        enum class DepthMode
        {
            ReadWrite,
            ReadOnly,
            Disabled,
            Occluded
        };

        /// D3DPipeline 構築パラメータ
        struct D3DPipelineDesc
        {
            CullMode cull = CullMode::Back;
            FillMode fill = FillMode::Solid;
            BlendMode blend = BlendMode::Opaque;
            DepthMode depth = DepthMode::ReadWrite;
        };

        /// ラスタライザやブレンドなどの描画ステートを一括管理するクラス
        /// D3DCommandList::SetPipeline へ渡すと 3 つのステートをまとめて適用できる
        class D3DPipeline : public NonCopyable
        {
        public:
            /// パイプラインを生成する
            /// @return 生成失敗時も非nullのインスタンスを返すが、無効な状態となる
            [[nodiscard]] static std::unique_ptr<D3DPipeline> Create(const D3DPipelineDesc& desc);

            ~D3DPipeline();

            [[nodiscard]] bool IsValid() const noexcept;

            /// 構築時に指定されたパラメータを返す
            [[nodiscard]] const D3DPipelineDesc& Desc() const noexcept;

            [[nodiscard]] ID3D11RasterizerState* RasterizerState() const noexcept;
            [[nodiscard]] ID3D11BlendState* BlendState() const noexcept;
            [[nodiscard]] ID3D11DepthStencilState* DepthStencilState() const noexcept;

        private:
            explicit D3DPipeline(const D3DPipelineDesc& desc) noexcept;

            D3DPipelineDesc m_desc{};
            ComPtr<ID3D11RasterizerState> m_rasterizer;
            ComPtr<ID3D11BlendState> m_blend;
            ComPtr<ID3D11DepthStencilState> m_depthStencil;
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS
