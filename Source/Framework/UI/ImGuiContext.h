#pragma once

/// @file ImGuiContext.h
/// @brief NS::UI::ImGuiContext — Dear ImGui (Win32 + DX11 backend) ライフサイクル管理
///
/// @details Debug / Development build 限定で実 ImGui を初期化し、 GameDebug / GameRelease
/// build では完全 stub (`IsUsingFallback() == true`) として全 API が何もしない
/// 公開ヘッダから `<imgui.h>` / `<imgui_impl_*.h>` を露出させない pImpl 標準形
/// 多重インスタンス禁止 — `ImGui::CreateContext()` がプロセス global のため、
/// Application が単一所有する想定。 構築失敗時は例外を投げず `IsValid() == false` +
/// `NS_LOG_ERROR` で詳細を残す

#include <cstdint>
#include <memory>

namespace NS::Platform
{
    class Window;
}

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::UI
{

    /// Dear ImGui (Win32 + DX11 backend) ライフサイクル管理。単一インスタンス前提 (copy/move 禁止)
    class ImGuiContext
    {
    public:
        struct Impl;

        /// CHECKVERSION → CreateContext → ImplWin32_Init → ImplDX11_Init を順に実行する。失敗時は IsValid()==false
        ImGuiContext(NS::Platform::Window& window, NS::Graphics::Renderer& renderer) noexcept;
        ~ImGuiContext() noexcept;

        ImGuiContext(const ImGuiContext&) = delete;
        ImGuiContext& operator=(const ImGuiContext&) = delete;
        ImGuiContext(ImGuiContext&&) = delete;
        ImGuiContext& operator=(ImGuiContext&&) = delete;

        /// 初期化成功なら true。 stub mode (GameRelease) では常に false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 実機能が無効化されている (stub or 初期化失敗) と true
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// `Renderer::BeginFrame()` 直後に呼ぶ。NewFrame 3 関数を順に発火する
        void BeginFrame() noexcept;

        /// `Renderer::EndFrame()` 直前に呼ぶ。`ImGui::Render` + `ImGui_ImplDX11_RenderDrawData`
        void EndFrame() noexcept;

        /// WndProc 入口で最初に呼ぶ。戻り値 true なら ImGui がイベントを消費したのでゲーム側へ転送しない
        [[nodiscard]] bool ForwardWndProc(void* hwnd,
                                          std::uint32_t msg,
                                          std::uintptr_t wParam,
                                          std::intptr_t lParam) noexcept;

        /// NewFrame 後のみ有効。EditorMode のマウス入力判定で参照する
        [[nodiscard]] bool WantCaptureMouse() const noexcept;

        /// NewFrame 後のみ有効。テキスト入力中の Tab / Esc 等をゲームに流さない判定に使う
        [[nodiscard]] bool WantCaptureKeyboard() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace NS::UI
