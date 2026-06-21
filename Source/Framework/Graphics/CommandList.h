#pragma once

/// @file CommandList.h
/// @brief NS::Graphics::CommandList — ID3D11DeviceContext の薄ラッパ。bind / clear / draw を集約
///
/// @details context は Renderer が所有し、 CommandList は非所有ポインタとして借用する。 描画リソース
/// (Buffer / Texture / TextureArray / Shader) を public アクセサ (`Native()` / `Srv()` / `Type()` 等)
/// 経由で参照し、 `IASetVertexBuffers` 等の D3D11 呼び出しをここに集約する。 取得は `Renderer::Commands()` で、
/// 呼び出し側は得た CommandList の bind / draw / update を直接呼ぶ
/// 値変換や複数ステージへの一括設定を持つ操作のみラップし、 それ以外の D3D 呼び出しは `operator->` で生 context
/// を直接叩く 依存: context の所有・寿命は Renderer。 本型は context を破棄しない 注: instanced 描画 (InstanceBatcher)
/// は 2-stream など専用要件のため独自の context 経路を維持する

#include <cstddef>

#include <Framework/Graphics/D3dCommon.h>

namespace NS::Graphics
{
    class Buffer;
    class Texture;
    class TextureArray;
    class Shader;
    class Pipeline;
    enum class ShaderType;
    enum class Topology;

    /// 1 つの `ID3D11DeviceContext` に bind / clear / draw を発行する記録面
    /// Renderer が 1 個保持し、 描画コードは `Renderer::Commands()` で取得して記録する
    class CommandList
    {
    public:
        explicit CommandList(ID3D11DeviceContext* context) noexcept;

        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;
        CommandList(CommandList&&) = delete;
        CommandList& operator=(CommandList&&) = delete;

        /// 描画先 (RTV + 任意 DSV) を設定する (OMSetRenderTargets)
        void SetRenderTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv) noexcept;
        /// RTV を単色クリアする。rtv==nullptr は何もしない
        void ClearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a) noexcept;
        /// DSV を depth + stencil クリアする。dsv==nullptr は何もしない
        void ClearDepth(ID3D11DepthStencilView* dsv, float depth = 1.0f) noexcept;
        /// 単一ビューポートを左上 0,0 起点で設定する (RSSetViewports)
        void SetViewport(float width, float height) noexcept;

        /// Pipeline の固定機能ステート一式を一括適用する (RS / OM blend / OM depth-stencil)
        /// 無効 Pipeline は何もしない。個別 Set の呼び忘れによるステート残留はこれで防ぐ
        void SetPipeline(const Pipeline& pipeline) noexcept;

        /// Shader をステージに応じて発行する (VS/PS/GS/HS/DS/CSSetShader)。無効 Shader は何もしない
        void SetShader(const Shader& shader) noexcept;
        /// Texture の SRV を slot + 指定ステージにバインドする (bind 対応は Vertex / Pixel のみ)
        void SetTexture(const Texture& texture, unsigned slot, ShaderType stage) noexcept;
        /// TextureArray の SRV を slot + 指定ステージにバインドする (bind 対応は Vertex / Pixel のみ)
        void SetTextureArray(const TextureArray& textureArray, unsigned slot, ShaderType stage) noexcept;
        /// サンプラーを slot + 指定ステージにバインドする (CommonStates の生 ID3D11SamplerState* を受ける)
        void SetSampler(ID3D11SamplerState* sampler, unsigned slot, ShaderType stage) noexcept;
        /// InputLayout を IA にバインドする (IASetInputLayout)。レイアウトの所有は Mesh、 ここは bind のみ
        /// 無効 (nullptr) は何もしない
        void SetInputLayout(ID3D11InputLayout* layout) noexcept;
        /// 頂点バッファを slot にバインドする (IASetVertexBuffers)
        void SetVertexBuffer(const Buffer& buffer, unsigned slot = 0) noexcept;
        /// index バッファをバインドする (IASetIndexBuffer、 幅は Buffer の Format から)
        void SetIndexBuffer(const Buffer& buffer) noexcept;
        /// プリミティブ形状を IA に設定する (IASetPrimitiveTopology)。所有は Mesh、 ここは bind のみ
        void SetTopology(Topology topology) noexcept;
        /// 定数バッファを slot + 指定ステージにバインドする (bind 対応は Vertex / Pixel のみ)
        void SetConstantBuffer(const Buffer& buffer, unsigned slot, ShaderType stage) noexcept;
        /// Dynamic バッファを Map/Discard で更新する。Static や容量超過は NS_LOG_ERROR を出して何もしない
        void UpdateBuffer(const Buffer& buffer, const void* data, std::size_t bytes) noexcept;

        /// index 付き描画 (DrawIndexed)
        void DrawIndexed(unsigned indexCount) noexcept;

        /// index 無し描画 (Draw)。line list 等 index を持たない蓄積描画に使う
        void Draw(unsigned vertexCount) noexcept;

        /// 借用している ID3D11DeviceContext (非所有)。継ぎ目で生 D3D を扱う場合に使う
        [[nodiscard]] ID3D11DeviceContext* Native() const noexcept;

        /// ラップしていない D3D 呼び出しを生 context へ透過する (`cmd->IASetInputLayout(...)` 等)
        /// context 無効時は nullptr を返すため呼び出し側で有効性を保証すること
        [[nodiscard]] ID3D11DeviceContext* operator->() const noexcept;

    private:
        ID3D11DeviceContext* m_context; // 非所有 (Renderer が所有)
    };

} // namespace NS::Graphics
