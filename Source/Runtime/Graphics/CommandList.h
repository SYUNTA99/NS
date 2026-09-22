#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <cstddef>

namespace NS::Gfx
{
    class Buffer;
    class Texture;
    class Shader;
    class Pipeline;
    enum class Topology;

    //! @brief ID3D11DeviceContext を包むラッパー
    //! @details Renderer が 1 つだけ所有し、外は Renderer::Commands から借りて使う
    //! 変換や一括設定が要る処理だけをラップし、その他は operator-> で元の API を直接呼ぶ
    class CommandList : public NS::Core::NonCopyable
    {
    public:
        //--------------------------------------------------------
        //! @name 初期化
        //--------------------------------------------------------
        //!@{

        explicit CommandList(ID3D11DeviceContext* context) noexcept;

        //!@}
        //--------------------------------------------------------
        //! @name 描画先とクリア
        //--------------------------------------------------------
        //!@{

        //! 描画先のレンダーターゲットと深度バッファを設定する
        void SetRenderTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv) noexcept;
        //! 描画先を指定した色でクリアする
        void ClearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a) noexcept;
        //! 深度・ステンシルバッファをクリアする
        void ClearDepth(ID3D11DepthStencilView* dsv, float depth = 1.0f) noexcept;
        //! ビューポートのサイズを設定する
        void SetViewport(float width, float height) noexcept;

        //!@}
        //--------------------------------------------------------
        //! @name ステート / リソースのバインド
        //--------------------------------------------------------
        //!@{

        //! シェーダとブレンド・深度・ラスタライザを一括で適用する
        void SetPipeline(const Pipeline& pipeline) noexcept;
        //! 頂点の入力レイアウトを設定する
        void SetInputLayout(ID3D11InputLayout* layout) noexcept;
        //! 頂点バッファを設定する
        void SetVertexBuffer(const Buffer& buffer, unsigned slot = 0) noexcept;
        //! インデックスバッファを設定する
        void SetIndexBuffer(const Buffer& buffer) noexcept;
        //! インデックスを三角形と線分のどちらとして読むかを設定する
        void SetTopology(Topology topology) noexcept;
        //! 指定したデータでバッファの内容を更新する
        void UpdateSubresource(const Buffer& buffer, const void* data, std::size_t bytes) noexcept;
        //! @brief 指定したデータでテクスチャ全体を更新する
        //! @param[in] rowPitch 画像 1 行あたりのバイト数
        void UpdateSubresource(const Texture& texture, const void* data, unsigned rowPitch) noexcept;

        //!@}
        //--------------------------------------------------------
        //! @name シェーダー種類別バインド
        //--------------------------------------------------------

        //! @name 頂点シェーダー (VS)
        //!@{
        void VSSetShader(const Shader& shader) noexcept;
        void VSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void VSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void VSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //! @name ピクセルシェーダー (PS)
        //!@{
        void PSSetShader(const Shader& shader) noexcept;
        void PSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void PSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void PSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //! @name ジオメトリシェーダー (GS)
        //!@{
        void GSSetShader(const Shader& shader) noexcept;
        void GSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void GSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void GSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //! @name コンピュートシェーダー (CS)
        //!@{
        void CSSetShader(const Shader& shader) noexcept;
        void CSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void CSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void CSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //--------------------------------------------------------
        //! @name 描画
        //--------------------------------------------------------
        //!@{

        //! インデックス付きの描画
        void DrawIndexed(unsigned indexCount) noexcept;
        //! インデックスを使わない描画。線分の並びなどインデックスを持たない形に使う
        void Draw(unsigned vertexCount) noexcept;

        //!@}
        //--------------------------------------------------------
        //! @name 生アクセス
        //--------------------------------------------------------
        //!@{

        //! 非所有の ID3D11DeviceContext。継ぎ目で生の DX11 を扱う時に使う
        [[nodiscard]] ID3D11DeviceContext* Native() const noexcept;
        //! cmd->IASetInputLayout(...) 等、ラップしていない DX11 呼び出しを生 context へそのまま流す
        //! @note context 無効時は nullptr を返すため呼び出し側で有効性を確認すること
        [[nodiscard]] ID3D11DeviceContext* operator->() const noexcept;

        //!@}
    private:
        ID3D11DeviceContext* m_context; //!< 非所有。所有は Renderer
    };

} // namespace NS::Gfx