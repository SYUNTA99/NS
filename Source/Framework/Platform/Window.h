#pragma once

/// @file Window.h
/// @brief NS::Platform::Window — Win32 ウィンドウのラッパ (単一インスタンス前提)
///
/// @details 公開ヘッダから `<windows.h>` / HWND は露出させない。 Graphics 層は
/// `NativeHandle()` を `reinterpret_cast<HWND>` で取り出す。 構築失敗時は
/// `IsValid() == false` を返し例外は投げない (詳細は `NS_LOG_ERROR` に出力)
/// 入力転送先 `Input*` は `AttachInput()` で非所有ポインタとして登録する

#include "Framework/Core/Math.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace NS::UI
{
    class ImGuiContext;
}

namespace NS::Platform
{

    class Input;

    /// Window 構築パラメータ。タイトルは UTF-8 で渡す (内部で wide 変換)
    struct WindowDesc
    {
        std::string title = "NS";
        NS::Math::Size2D size{1280, 720};
        /// false で構築時に非表示 (SW_HIDE) 起動。Render テスト用に Window を見せないとき使う
        bool visible = true;
    };

    /// Win32 ウィンドウのラッパ
    /// 単一インスタンス前提 (HWND 所有権を固定するためコピー/ムーブ禁止)
    /// 公開ヘッダから <windows.h> / HWND は露出させない (Graphics 層は NativeHandle() を reinterpret_cast)
    class Window
    {
    public:
        /// 内部実装。定義は detail/win32_window.h にある
        /// 公開しているのは WndProc などの自由関数からアクセス可能にするためで、外部から触らないこと
        struct Impl;

        explicit Window(const WindowDesc& desc);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        /// 構築成功判定。RegisterClassExW / CreateWindowExW が失敗した時は false を返す
        /// 失敗時は NS_LOG_ERROR にも詳細が出ているが、呼び出し側は API 経路で検知できるようこれを参照する
        [[nodiscard]] bool IsValid() const noexcept;

        /// 1 フレーム頭で呼ぶ。PeekMessageW(PM_REMOVE) で非ブロッキング処理
        /// WM_QUIT を受信したら ShouldClose() が true になる
        void PollMessages() noexcept;

        [[nodiscard]] bool ShouldClose() const noexcept;

        [[nodiscard]] NS::Math::Size2D Size() const noexcept;

        /// HWND を void* で公開。Graphics 層は reinterpret_cast<HWND> で取り出す
        [[nodiscard]] void* NativeHandle() const noexcept;

        /// UTF-8 入力でタイトル変更
        void SetTitle(std::string_view utf8Title) noexcept;

        /// 自身に WM_CLOSE を投げて閉じ要求を出す (× ボタンと同じ経路)
        /// SetCloseCallback が登録されていればそこに通知、未設定なら PostQuitMessage に落ちて
        /// 次回 PollMessages 後に ShouldClose() が true になる
        void RequestClose() noexcept;

        /// リサイズ通知 (WM_SIZE)。最小化中は呼ばれない
        void SetResizeCallback(std::function<void(NS::Math::Size2D)> cb);

        /// ×ボタン等で閉じる要求 (WM_CLOSE) を受け取った時に呼ばれる
        /// callback 内で RequestClose() を呼ばないと閉じない (拒否可能)
        void SetCloseCallback(std::function<void()> cb);

        /// 入力ターゲットを設定する。WndProc が WM_KEYDOWN / WM_KEYUP / WM_KILLFOCUS を
        /// 受信した時にこの Input へ転送する。非所有ポインタ (Application が所有)
        /// nullptr 解除可
        void AttachInput(Input* input) noexcept;

        /// ImGui コンテキストを登録する。 WndProc 入口で先に ImGui へ message を
        /// forward し、 ImGui がキャプチャ中 (WantCaptureMouse / WantCaptureKeyboard)
        /// なら Input への転送を抑止する。 非所有ポインタ、 nullptr 解除可
        void AttachImGui(NS::UI::ImGuiContext* imgui) noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace NS::Platform
