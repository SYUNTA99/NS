#pragma once

#include <memory>

namespace ns::graphics
{

    class Renderer;

    /// DirectXTK CommonStates のラッパ (//)。
    /// 公開セット = 10 getter (Blend 2 / Depth 2 / Rasterizer 2 / Sampler 4)。
    /// 戻り値は void* で D3D11 型を公開ヘッダに漏らさない。
    /// 利用側 (Buffer/Texture/Material) は detail/d3d_context.h 経由で
    /// reinterpret_cast<ID3D11BlendState*> 等に戻す。
    class CommonStates
    {
    public:
        struct Impl;

        ~CommonStates();

        CommonStates(const CommonStates&) = delete;
        CommonStates& operator=(const CommonStates&) = delete;
        CommonStates(CommonStates&&) = delete;
        CommonStates& operator=(CommonStates&&) = delete;

        [[nodiscard]] void* Opaque() const noexcept;
        [[nodiscard]] void* AlphaBlend() const noexcept;

        [[nodiscard]] void* DepthDefault() const noexcept;
        [[nodiscard]] void* DepthNone() const noexcept;

        [[nodiscard]] void* CullCounterClockwise() const noexcept;
        [[nodiscard]] void* CullClockwise() const noexcept;

        [[nodiscard]] void* LinearWrap() const noexcept;
        [[nodiscard]] void* LinearClamp() const noexcept;
        [[nodiscard]] void* PointWrap() const noexcept;
        [[nodiscard]] void* PointClamp() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend class Renderer;
        explicit CommonStates(void* device);
    };

} // namespace ns::graphics
