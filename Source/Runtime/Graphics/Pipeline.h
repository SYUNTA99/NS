#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <memory>

namespace NS::Graphics
{

    //! ポリゴンのどちらの面を描画せずにカリングするか
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

    //! 描画する色と背景色をどのように合成するか
    enum class BlendMode
    {
        Opaque,
        Alpha,
        Additive
    };

    //! @brief 深度の読み書き設定
    //! @details ReadOnly は手前かどうかを判定するが深度を書かない。半透明と背景に使う
    enum class DepthMode
    {
        ReadWrite,
        ReadOnly,
        Disabled
    };

    //! Pipeline 構築パラメータ
    struct PipelineDesc
    {
        CullMode cull = CullMode::Back;
        FillMode fill = FillMode::Solid;
        BlendMode blend = BlendMode::Opaque;
        DepthMode depth = DepthMode::ReadWrite;
    };

    //! @brief ラスタライザやブレンドなどの描画ステートを一括管理するクラス
    //! @details CommandList::SetPipeline へ渡すと 3 つのステートをまとめて適用できる
    class Pipeline : public NS::Core::NonCopyable
    {
    public:
        //! @brief パイプラインを生成する
        //! @param[in] desc 初期化パラメータ
        //! @return 生成失敗時も非nullのインスタンスを返すが、無効な状態となる
        [[nodiscard]] static std::unique_ptr<Pipeline> Create(const PipelineDesc& desc);

        ~Pipeline();

        [[nodiscard]] bool IsValid() const noexcept;

        //! 構築時に指定されたパラメータを返す
        [[nodiscard]] const PipelineDesc& Desc() const noexcept;

        [[nodiscard]] ID3D11RasterizerState* RasterizerState() const noexcept;
        [[nodiscard]] ID3D11BlendState* BlendState() const noexcept;
        [[nodiscard]] ID3D11DepthStencilState* DepthStencilState() const noexcept;

    private:
        explicit Pipeline(const PipelineDesc& desc) noexcept;

        PipelineDesc m_desc{};
        ComPtr<ID3D11RasterizerState> m_rasterizer;
        ComPtr<ID3D11BlendState> m_blend;
        ComPtr<ID3D11DepthStencilState> m_depthStencil;
        bool m_valid = false;
    };

} // namespace NS::Graphics
