#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <cstddef>

namespace NS::Graphics
{
    class Buffer;
    class Texture;
    class TextureArray;
    class Shader;
    class Pipeline;
    enum class Topology;

    //! @brief グラフィックスAPIのデバイスコンテキストを包んだラッパークラス。
    //! @details リソースの設定、画面のクリア、描画の発行といった基本操作を集約する。
    //! Rendererが単一のインスタンスを所有し、外部からはそれを経由して利用する設計となっている
    //! 変換や一括設定が必要な処理のみをラップし、その他は `operator->` 経由で元のAPIを直接呼び出す設計
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

        //! 描画先（レンダーターゲットと深度バッファ）を設定する
        void SetRenderTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv) noexcept;
        //! 描画先を指定した色でクリアする
        void ClearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a) noexcept;
        //! 深度・ステンシルバッファをクリアする
        void ClearDepth(ID3D11DepthStencilView* dsv, float depth = 1.0f) noexcept;
        //! 描画領域（ビューポート）のサイズを設定する
        void SetViewport(float width, float height) noexcept;

        //!@}
        //--------------------------------------------------------
        //! @name ステート / リソースのバインド
        //--------------------------------------------------------
        //!@{

        //! 描画設定（パイプライン）を一括で適用する
        void SetPipeline(const Pipeline& pipeline) noexcept;
        //! 頂点の入力レイアウトを設定する
        void SetInputLayout(ID3D11InputLayout* layout) noexcept;
        //! 頂点バッファを設定する
        void SetVertexBuffer(const Buffer& buffer, unsigned slot = 0) noexcept;
        //! インデックスバッファを設定する
        void SetIndexBuffer(const Buffer& buffer) noexcept;
        //! 描画する図形の形状を設定する
        void SetTopology(Topology topology) noexcept;
        //! 指定したデータでバッファの内容を更新する
        void UpdateSubresource(const Buffer& buffer, const void* data, std::size_t bytes) noexcept;
        //! @brief 指定したデータでテクスチャ全体を更新する。
        //! @param rowPitch 画像1行あたりのデータサイズ（バイト数）
        void UpdateSubresource(const Texture& texture, const void* data, unsigned rowPitch) noexcept;

        //!@}
        //--------------------------------------------------------
        //! @name シェーダー種類別バインド
        //--------------------------------------------------------

        //! @name 頂点シェーダー (VS)
        //!@{
        //! 頂点シェーダーを設定
        void VSSetShader(const Shader& shader) noexcept;
        void VSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void VSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept;
        void VSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void VSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //! @name ピクセルシェーダー (PS)
        //!@{
        //! ピクセルシェーダーを設定
        void PSSetShader(const Shader& shader) noexcept;
        void PSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void PSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept;
        void PSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void PSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //! @name ジオメトリシェーダー (GS)
        //!@{
        //! ジオメトリシェーダーを設定
        void GSSetShader(const Shader& shader) noexcept;
        void GSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void GSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept;
        void GSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void GSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //! @name コンピュートシェーダー (CS)
        //!@{
        //! コンピュートシェーダーを設定
        void CSSetShader(const Shader& shader) noexcept;
        void CSSetShaderResource(const Texture& texture, unsigned slot) noexcept;
        void CSSetShaderResource(const TextureArray& textureArray, unsigned slot) noexcept;
        void CSSetSampler(ID3D11SamplerState* sampler, unsigned slot) noexcept;
        void CSSetConstantBuffer(const Buffer& buffer, unsigned slot) noexcept;
        //!@}

        //--------------------------------------------------------
        //! @name 描画
        //--------------------------------------------------------
        //!@{

        //! DrawIndexed による index 付き描画
        void DrawIndexed(unsigned indexCount) noexcept;
        //! Draw による index 無し描画。line list 等 index を持たない蓄積描画に使う
        void Draw(unsigned vertexCount) noexcept;

        //!@}
        //--------------------------------------------------------
        //! @name 生アクセス
        //--------------------------------------------------------
        //!@{

        //! 借用している非所有の ID3D11DeviceContext。継ぎ目で生 D3D を扱う場合に使う
        [[nodiscard]] ID3D11DeviceContext* Native() const noexcept;
        //! cmd->IASetInputLayout(...) 等、ラップしていない D3D 呼び出しを生 context へそのまま流す
        //! @note context 無効時は nullptr を返すため呼び出し側で有効性を確認すること
        [[nodiscard]] ID3D11DeviceContext* operator->() const noexcept;

        //!@}
    private:
        ID3D11DeviceContext* m_context; //!< 非所有。所有は Renderer
    };

} // namespace NS::Graphics