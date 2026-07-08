#pragma once

/// @file CommonStates.h
/// @brief NS::Graphics::CommonStates — DirectXTK CommonStates のラッパ
///
/// @details 構築は friend である `Renderer` のみが行い、外部から直接コンストラクタは呼べない

#include <memory>

#include "Framework/Core/NonCopyable.h"
#include "Framework/Graphics/D3dCommon.h"

namespace DirectX
{
    inline namespace DX11
    {
        class CommonStates;
    }
} // namespace DirectX

namespace NS::Graphics
{

    class Renderer;

    /// DirectXTK CommonStates のラッパ。公開セットは Blend 2 / Depth 2 / Rasterizer 2 / Sampler 4 の 10 getter
    /// 戻り値は型付き D3D11 state ポインタで、Renderer / Material が直接 bind する
    class CommonStates : public NS::Core::NonCopyable
    {
    public:
        ~CommonStates();

        /// 構築成功判定。DirectXTK CommonStates の生成に失敗すると false を返し、各 getter は nullptr を返す
        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] ID3D11BlendState* Opaque() const noexcept;
        [[nodiscard]] ID3D11BlendState* AlphaBlend() const noexcept;

        [[nodiscard]] ID3D11DepthStencilState* DepthDefault() const noexcept;
        [[nodiscard]] ID3D11DepthStencilState* DepthNone() const noexcept;

        [[nodiscard]] ID3D11RasterizerState* CullCounterClockwise() const noexcept;
        [[nodiscard]] ID3D11RasterizerState* CullClockwise() const noexcept;

        [[nodiscard]] ID3D11SamplerState* LinearWrap() const noexcept;
        [[nodiscard]] ID3D11SamplerState* LinearClamp() const noexcept;
        [[nodiscard]] ID3D11SamplerState* PointWrap() const noexcept;
        [[nodiscard]] ID3D11SamplerState* PointClamp() const noexcept;

    private:
        std::unique_ptr<DirectX::CommonStates> m_states;

        friend class Renderer;
        explicit CommonStates(ID3D11Device* device) noexcept;
    };

} // namespace NS::Graphics
