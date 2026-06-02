#pragma once

/// @file Renderer.h
/// @brief NS::Graphics::Renderer — D3D11 Device / DeviceContext / SwapChain を
/// 所有する描画ファサード
///
/// @details Window と 1 対 1 で生成し、 Window のリサイズ通知を購読する
/// 公開ヘッダから `<d3d11.h>` / `<dxgi.h>` を漏らさないため pImpl 標準形
/// 内部 D3D ハンドルは `detail::GetDevice/GetContext/GetSwapChain` で取得する
/// 構築失敗時は `IsValid() == false` を返し例外は投げない (`NS_LOG_ERROR` に詳細出力)

#include <memory>

#include <Framework/Platform/Window.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace NS::Graphics
{

    /// Renderer 構築パラメータ。現状は最小フィールドのみ
    /// MSAA / HDR / FEATURE_LEVEL 切替は将来追加予定
    struct RendererDesc
    {
        /// D3D11 Debug Layer を有効化する。Debug ビルドで true 推奨
        bool enableDebugLayer = false;
        /// Present 時の V-Sync (true で SyncInterval=1、false で 0)
        bool vsync = true;
    };

    class Renderer;
    class RenderTarget;
    class CommonStates;

    namespace detail
    {
        /// detail/d3d_context.h で再宣言される typed accessor
        /// Renderer の friend として private impl にアクセスする
        [[nodiscard]] ID3D11Device* GetDevice(Renderer& renderer) noexcept;
        [[nodiscard]] ID3D11DeviceContext* GetContext(Renderer& renderer) noexcept;
        [[nodiscard]] IDXGISwapChain* GetSwapChain(Renderer& renderer) noexcept;
    } // namespace detail

    /// D3D11 Device / DeviceContext / SwapChain を所有するレンダラ
    /// Window と 1 対 1 で生成し、Window のリサイズ通知を購読する
    /// 公開ヘッダから <d3d11.h> / <dxgi.h> を漏らさないため pImpl 標準形
    /// 内部 D3D ハンドルは detail::GetDevice/GetContext/GetSwapChain で取得する
    class Renderer
    {
    public:
        struct Impl;

        Renderer(const RendererDesc& desc, ::NS::Platform::Window& window);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;

        /// 構築成功判定。D3D11CreateDevice / SwapChain 作成失敗時に false
        [[nodiscard]] bool IsValid() const noexcept;

        /// フレーム頭で呼ぶ。MainRenderTarget をクリア + Bind する薄ラッパ
        void BeginFrame(float r, float g, float b, float a) noexcept;

        /// フレーム末で呼ぶ。SwapChain::Present を実行する
        void EndFrame() noexcept;

        /// SwapChain::ResizeBuffers + 主 RT 再構築。Window リサイズで自動呼出される
        /// size.width または size.height が 0 以下なら no-op (最小化対応)
        void Resize(NS::Math::Size2D size) noexcept;

        [[nodiscard]] NS::Math::Size2D Size() const noexcept;

        /// Backbuffer 主 RT。Renderer 寿命と同期、別 Window では使えない
        [[nodiscard]] RenderTarget& MainRenderTarget() noexcept;

        /// DirectXTK CommonStates ラッパ。Mesh/Material 等が利用
        [[nodiscard]] CommonStates& States() noexcept;

        /// 内部 ID3D11Device を非 detail 経路で公開。 ImGui_ImplDX11_Init など
        /// 外部 SDK が D3D11 ハンドルを直接必要とする場合のみ使う
        /// 通常の Graphics ロジックは `detail::GetDevice` 経由を推奨
        [[nodiscard]] ID3D11Device* NativeDevice() noexcept;

        /// 内部 ID3D11DeviceContext を非 detail 経路で公開。 用途は `NativeDevice()` と同じ
        [[nodiscard]] ID3D11DeviceContext* NativeContext() noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11Device* detail::GetDevice(Renderer& renderer) noexcept;
        friend ID3D11DeviceContext* detail::GetContext(Renderer& renderer) noexcept;
        friend IDXGISwapChain* detail::GetSwapChain(Renderer& renderer) noexcept;
    };

} // namespace NS::Graphics
