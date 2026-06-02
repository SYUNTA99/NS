#pragma once

/// @file RenderTarget.h
/// @brief NS::Graphics::RenderTarget — Color + 任意 Depth のラッパ
///
/// @details 現状は `Renderer` が所有する Backbuffer 専用。将来 offscreen RT を追加する際に
/// `RenderTargetDesc` を公開し、任意サイズの RT も作れるようにする予定

#include "Framework/Core/Math.h"

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace NS::Graphics
{

    /// RenderTarget 構築パラメータ。現状は Backbuffer 専用なので外部は触らない
    /// 将来 offscreen RT を追加した時に公開される予定
    struct RenderTargetDesc
    {
        NS::Math::Size2D size{0, 0};
        /// false なら Color のみ (UI / post-process 用、現状未使用)
        bool createDepth = true;
    };

    class Renderer;

    /// 描画先の Color バッファ (+ 任意で Depth) と、その RTV/DSV を 1 つにまとめたラッパ
    /// 生成できるのは friend の Renderer だけ (コンストラクタを private にして外からの生成を禁止)
    /// クリア/バインド/リサイズはこのクラス内で完結し、呼び出し側は RTV/DSV を直接扱わない
    class RenderTarget
    {
    public:
        struct Impl;

        ~RenderTarget();

        RenderTarget(const RenderTarget&) = delete;
        RenderTarget& operator=(const RenderTarget&) = delete;
        RenderTarget(RenderTarget&&) = delete;
        RenderTarget& operator=(RenderTarget&&) = delete;

        /// Color/Depth を指定値でクリア。Depth が無い場合 depth 引数は無視
        void Clear(float r, float g, float b, float a, float depth = 1.0f) noexcept;

        /// OMSetRenderTargets + RSSetViewports を同時設定する
        void Bind() noexcept;

        /// Backbuffer の場合 SwapChain::ResizeBuffers → RTV/DSV 再構築
        /// size.width/size.height が 0 以下なら no-op
        void Resize(NS::Math::Size2D size) noexcept;

        [[nodiscard]] NS::Math::Size2D Size() const noexcept;
        [[nodiscard]] bool HasDepth() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend class Renderer;
        RenderTarget();

        /// Renderer コンストラクタ内でのみ呼ばれる。SwapChain から backbuffer texture を取得し
        /// RTV + (任意で) DSV を構築する。失敗時 false
        bool ConfigureAsBackbuffer(IDXGISwapChain* swapchain,
                                   ID3D11Device* device,
                                   ID3D11DeviceContext* context,
                                   bool createDepth) noexcept;
    };

} // namespace NS::Graphics
