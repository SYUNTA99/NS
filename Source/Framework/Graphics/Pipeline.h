#pragma once

/// @file Pipeline.h
/// @brief NS::Graphics::Pipeline — rasterizer / blend / depth-stencil を 1 束で適用する固定機能ステート
///
/// @details DX11 はステートが分離しており、 個別 Set の呼び忘れは「前の描画の状態が残ったまま描く」事故になる
/// PipelineDesc は NS がサポートする組合せを意図レベルの enum の閉集合で表し、 Create 時に
/// D3D11 ステートオブジェクトへ変換して保持する。 適用は `CommandList::SetPipeline` で一括
/// 生成は重いので初期化時に作り、 描画ループ中はセットのみ行うこと
/// シェーダは Material、 InputLayout / topology は Mesh の所有 (固定機能ステートのみ束ねる)

#include <memory>

#include <Framework/Core/NonCopyable.h>
#include <Framework/Graphics/D3dCommon.h>

namespace NS::Graphics
{

    /// カリング面。Front は inside-out cube (skybox) 用
    enum class CullMode
    {
        None,
        Back,
        Front
    };

    /// 塗りつぶし方式
    enum class FillMode
    {
        Solid,
        Wireframe
    };

    /// ブレンド方式。Alpha は通常半透明、Additive は加算合成
    enum class BlendMode
    {
        Opaque,
        Alpha,
        Additive
    };

    /// 深度の扱い。ReadOnly は比較 LESS_EQUAL + 書込 OFF (z=1 張り付きの skybox / 半透明用)
    enum class DepthMode
    {
        ReadWrite,
        ReadOnly,
        Disabled
    };

    /// Pipeline 構築パラメータ。既定値は不透明描画の組
    struct PipelineDesc
    {
        CullMode cull = CullMode::Back;
        FillMode fill = FillMode::Solid;
        BlendMode blend = BlendMode::Opaque;
        DepthMode depth = DepthMode::ReadWrite;
    };

    /// 固定機能ステートの束。初期化時に Create し、描画は CommandList::SetPipeline で一括適用する
    class Pipeline : public NS::Core::NonCopyable
    {
    public:
        /// PipelineDesc から D3D11 ステートオブジェクトを構築する。 失敗時も非 null (IsValid() で検知)
        [[nodiscard]] static std::unique_ptr<Pipeline> Create(const PipelineDesc& desc);

        ~Pipeline();

        /// 全ステートオブジェクトの構築成功なら true。Device 不在や Create 失敗で false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 構築時の意図 (テスト観測・デバッグ表示用)
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
