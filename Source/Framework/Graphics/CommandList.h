#pragma once

/// @file CommandList.h
/// @brief NS::Graphics::CommandList — ID3D11DeviceContext の薄ラッパ。bind / clear / draw を集約
///
/// @details context は Renderer が所有し、 CommandList は非所有ポインタとして借用する。 描画リソース
/// (Buffer / Texture / TextureArray / Shader) を public アクセサ (`Native()` / `Srv()` / `Type()` 等)
/// 経由で参照し、 `IASetVertexBuffers` 等の D3D11 呼び出しをここに集約する。 取得は `Renderer::Commands()`、
/// `Renderer::BindX` 系は本クラスへ転送する薄い facade
/// 依存: context の所有・寿命は Renderer。 本型は context を破棄しない
/// 注: instanced 描画 (InstanceBatcher) は 2-stream など専用要件のため独自の context 経路を維持する

#include <cstddef>

#include <d3d11.h>

namespace NS::Graphics
{
    class Buffer;
    class Texture;
    class TextureArray;
    class Shader;
    enum class ShaderStage : unsigned;

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
        /// RTV を単色クリアする。rtv==nullptr は no-op
        void ClearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a) noexcept;
        /// DSV を depth + stencil クリアする。dsv==nullptr は no-op
        void ClearDepth(ID3D11DepthStencilView* dsv, float depth = 1.0f) noexcept;
        /// 単一ビューポートを左上 0,0 起点で設定する (RSSetViewports)
        void SetViewport(float width, float height) noexcept;

        /// Shader をステージに応じて発行する (VS/PS/GS/HS/DS/CSSetShader)。無効 Shader は no-op
        void SetShader(const Shader& shader) noexcept;
        /// Texture の SRV を slot + ステージ (VS/PS/GS) にバインドする
        void SetTexture(const Texture& texture, unsigned slot, ShaderStage stages) noexcept;
        /// TextureArray の SRV を slot + ステージ (VS/PS/GS) にバインドする
        void SetTextureArray(const TextureArray& textureArray, unsigned slot, ShaderStage stages) noexcept;
        /// 頂点バッファを slot にバインドする (IASetVertexBuffers)
        void SetVertexBuffer(const Buffer& buffer, unsigned slot = 0) noexcept;
        /// index バッファをバインドする (IASetIndexBuffer、 幅は Buffer の Format から)
        void SetIndexBuffer(const Buffer& buffer) noexcept;
        /// 定数バッファを slot + ステージ (VS/PS/GS) にバインドする
        void SetConstantBuffer(const Buffer& buffer, unsigned slot, ShaderStage stages) noexcept;
        /// Dynamic バッファを Map/Discard で更新する。Static や容量超過は NS_LOG_ERROR + no-op
        void UpdateBuffer(const Buffer& buffer, const void* data, std::size_t bytes) noexcept;

        /// index 付き描画 (DrawIndexed)
        void DrawIndexed(unsigned indexCount) noexcept;

        /// 借用している ID3D11DeviceContext (非所有)。継ぎ目で raw D3D を扱う場合に使う
        [[nodiscard]] ID3D11DeviceContext* Native() const noexcept;

    private:
        ID3D11DeviceContext* m_context; // 非所有 (Renderer が所有)
    };

} // namespace NS::Graphics
