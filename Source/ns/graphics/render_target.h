#pragma once

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace ns::graphics
{

    /// RenderTarget 構築パラメータ。 は Backbuffer 専用なので外部は触らない。
    /// 将来 offscreen RT を追加した時 (+) に公開される予定。
    struct RenderTargetDesc
    {
        int width = 0;
        int height = 0;
        /// false なら Color のみ (UI / post-process 用、 未使用)。
        bool createDepth = true;
    };

    class Renderer;

    /// レンダーターゲット (Color + 任意で Depth) のラッパ。
    ///  は Renderer が所有する Backbuffer 専用、ctor は private。
    /// Clear / Bind / Resize は自己完結、OOP らしく自分の RTV/DSV を扱う (/)。
    class RenderTarget
    {
    public:
        struct Impl;

        ~RenderTarget();

        RenderTarget(const RenderTarget&) = delete;
        RenderTarget& operator=(const RenderTarget&) = delete;
        RenderTarget(RenderTarget&&) = delete;
        RenderTarget& operator=(RenderTarget&&) = delete;

        /// Color/Depth を指定値でクリア。Depth が無い場合 depth 引数は無視。
        void Clear(float r, float g, float b, float a, float depth = 1.0f) noexcept;

        /// OMSetRenderTargets + RSSetViewports を同時設定する。
        void Bind() noexcept;

        /// Backbuffer の場合 SwapChain::ResizeBuffers → RTV/DSV 再構築。
        /// width/height が 0 以下なら no-op。
        void Resize(int width, int height) noexcept;

        [[nodiscard]] int Width() const noexcept;
        [[nodiscard]] int Height() const noexcept;
        [[nodiscard]] bool HasDepth() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend class Renderer;
        RenderTarget();

        /// Renderer ctor 内でのみ呼ばれる。SwapChain から backbuffer texture を取得し
        /// RTV + (任意で) DSV を構築する。失敗時 false。
        bool ConfigureAsBackbuffer(IDXGISwapChain* swapchain,
                                   ID3D11Device* device,
                                   ID3D11DeviceContext* context,
                                   bool createDepth) noexcept;
    };

} // namespace ns::graphics
