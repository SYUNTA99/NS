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

    //! 描画する色と背景色をどのように合成するか。
    enum class BlendMode
    {
        Opaque,
        Alpha,
        Additive
    };

    //! @brief 奥行き（深度）の読み書き設定
    //! @details
    //! 読み取り専用（ReadOnly）は、手前にあるかどうかの判定は行うが深度情報の更新はしない設定で、半透明オブジェクトや背景の描画に使用する
    enum class DepthMode
    {
        ReadWrite,
        ReadOnly,
        Disabled
    };

    /// Pipeline 構築パラメータ
    struct PipelineDesc
    {
        CullMode cull = CullMode::Back;
        FillMode fill = FillMode::Solid;
        BlendMode blend = BlendMode::Opaque;
        DepthMode depth = DepthMode::ReadWrite;
    };

    //! @brief ラスタライザやブレンドなどの描画ステートを一括管理するクラス
    //! @details 描画システムにまとめて適用することで、描画設定を効率的に切り替えるために使用する
    class Pipeline : public NS::Core::NonCopyable
    {
    public:
        //! @brief パイプラインを生成する。
        //! @param desc 初期化パラメータ。
        //! @return 生成失敗時も非nullのインスタンスを返すが、無効な状態となる。
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
