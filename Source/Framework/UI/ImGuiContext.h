#pragma once

/// @file ImGuiContext.h
/// @brief NS::UI::ImGuiContext — Win32 + DX11 backend の Dear ImGui ライフサイクル管理
///
/// @details UI 層は GameRelease では premake kind None で丸ごとビルドされない。Debug / Development /
/// GameDebug でのみ実 ImGui を初期化する。出荷から editor を物理排除するため Framework は UI へ依存せず、
/// EditorLayer が本 context を単一所有して NewFrame / Render を駆動する
/// 公開ヘッダから `<imgui.h>` / `<imgui_impl_*.h>` を露出させない pImpl 標準形
/// 多重インスタンス禁止 — `ImGui::CreateContext()` がプロセス global のため単一所有が前提
/// 構築失敗時は例外を投げず `IsValid() == false` + `NS_LOG_ERROR` で詳細を残す

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

    /// Win32 + DX11 backend の Dear ImGui ライフサイクル管理。copy/move 禁止の単一インスタンス前提
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

        /// 初期化成功なら true。 GameRelease の stub mode では常に false
        [[nodiscard]] bool IsValid() const noexcept;

        /// stub または初期化失敗で実機能が無効化されていると true
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
