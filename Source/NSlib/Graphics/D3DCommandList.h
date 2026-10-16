#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <cstddef>

namespace NS
{
    namespace Gfx
    {
        class D3DBuffer;
        class D3DTexture;
        class D3DShader;
        class D3DPipeline;
        enum class Topology;

        //! Renderer が所有する描画コンテキスト。変換や一括設定はメソッド、それ以外は operator-> を使う
        class D3DCommandList : public NonCopyable
        {
        public:
            //--------------------------------------------------------
            //! @name 描画先とクリア
            //--------------------------------------------------------
            //!@{

            void SetRenderTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv) noexcept;
            void ClearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a) noexcept;
            //! 深度・ステンシルバッファをクリアする
            void ClearDepth(ID3D11DepthStencilView* dsv, float depth = 1.0f) noexcept;
            void SetViewport(float width, float height) noexcept;

            //!@}
            //--------------------------------------------------------
            //! @name ステート / リソースのバインド
            //--------------------------------------------------------
            //!@{

            //! シェーダとブレンド・深度・ラスタライザを一括で適用する
            void SetPipeline(const D3DPipeline& pipeline) noexcept;
            void SetInputLayout(ID3D11InputLayout* layout) noexcept;
            void SetVertexBuffer(const D3DBuffer& buffer, uint slot = 0) noexcept;
            void SetIndexBuffer(const D3DBuffer& buffer) noexcept;
            void SetTopology(Topology topology) noexcept;
            //! 指定したデータでバッファの内容を更新する
            void UpdateSubresource(const D3DBuffer& buffer, const void* data, std::size_t bytes) noexcept;
            //! @brief 指定したデータでテクスチャ全体を更新する
            //! @param[in] rowPitch 画像 1 行あたりのバイト数
            void UpdateSubresource(const D3DTexture& texture, const void* data, uint rowPitch) noexcept;

            //!@}
            //--------------------------------------------------------
            //! @name シェーダー種類別バインド
            //--------------------------------------------------------

            //! @name 頂点シェーダー (VS)
            //!@{
            void VSSetShader(const D3DShader& shader) noexcept;
            void VSSetShaderResource(const D3DTexture& texture, uint slot) noexcept;
            void VSSetSampler(ID3D11SamplerState* sampler, uint slot) noexcept;
            void VSSetConstantBuffer(const D3DBuffer& buffer, uint slot) noexcept;
            //!@}

            //! @name ピクセルシェーダー (PS)
            //!@{
            void PSSetShader(const D3DShader& shader) noexcept;
            void PSSetShaderResource(const D3DTexture& texture, uint slot) noexcept;
            void PSSetSampler(ID3D11SamplerState* sampler, uint slot) noexcept;
            void PSSetConstantBuffer(const D3DBuffer& buffer, uint slot) noexcept;
            //!@}

            //! @name ジオメトリシェーダー (GS)
            //!@{
            void GSSetShader(const D3DShader& shader) noexcept;
            void GSSetShaderResource(const D3DTexture& texture, uint slot) noexcept;
            void GSSetSampler(ID3D11SamplerState* sampler, uint slot) noexcept;
            void GSSetConstantBuffer(const D3DBuffer& buffer, uint slot) noexcept;
            //!@}

            //! @name コンピュートシェーダー (CS)
            //!@{
            void CSSetShader(const D3DShader& shader) noexcept;
            void CSSetShaderResource(const D3DTexture& texture, uint slot) noexcept;
            void CSSetSampler(ID3D11SamplerState* sampler, uint slot) noexcept;
            void CSSetConstantBuffer(const D3DBuffer& buffer, uint slot) noexcept;
            //!@}

            //--------------------------------------------------------
            //! @name 描画
            //--------------------------------------------------------
            //!@{

            void DrawIndexed(uint indexCount) noexcept;
            //! インデックスを使わない描画。線分の並びなどインデックスを持たない形に使う
            void Draw(uint vertexCount) noexcept;

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
            friend class Renderer;

            //! @brief Renderer が自分の immediate context を包んで作る
            //! @param[in] context 非所有の ID3D11DeviceContext
            explicit D3DCommandList(ID3D11DeviceContext* context) noexcept;

            ID3D11DeviceContext* m_context; //!< 非所有。所有は Renderer
        };
    } // namespace Gfx
} // namespace NS
