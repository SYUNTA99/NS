#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <memory>

namespace DirectX
{
    inline namespace DX11
    {
        class CommonStates;
    }
} // namespace DirectX

namespace NS
{
    namespace Gfx
    {
        class Renderer;

        //! Renderer が所有する DirectXTK の共通描画ステート
        class D3DCommonStates : public NonCopyable
        {
        public:
            ~D3DCommonStates();

            [[nodiscard]] bool IsValid() const noexcept;

            [[nodiscard]] ID3D11BlendState* Opaque() const noexcept;     //!< 不透明
            [[nodiscard]] ID3D11BlendState* AlphaBlend() const noexcept; //!< アルファブレンド

            [[nodiscard]] ID3D11DepthStencilState* DepthDefault() const noexcept; //!< 深度テスト＋書き込みあり
            [[nodiscard]] ID3D11DepthStencilState* DepthNone() const noexcept;    //!< 深度テスト＋書き込みなし

            [[nodiscard]] ID3D11RasterizerState* CullCounterClockwise() const noexcept; //!< 反時計回りの面をカリング
            [[nodiscard]] ID3D11RasterizerState* CullClockwise() const noexcept;        //!< 時計回りの面をカリング

            [[nodiscard]] ID3D11SamplerState* LinearWrap() const noexcept;  //!< 線形補間＋UV繰り返し
            [[nodiscard]] ID3D11SamplerState* LinearClamp() const noexcept; //!< 線形補間＋端で固定
            [[nodiscard]] ID3D11SamplerState* PointWrap() const noexcept;   //!< 点サンプリング＋UV繰り返し
            [[nodiscard]] ID3D11SamplerState* PointClamp() const noexcept;  //!< 点サンプリング＋端で固定

        private:
            std::unique_ptr<DirectX::CommonStates> m_states;

            friend class Renderer;
            explicit D3DCommonStates(ID3D11Device* device) noexcept;
        };
    } // namespace Gfx
} // namespace NS
