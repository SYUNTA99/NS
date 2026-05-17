#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ns::platform
{

    /// Window 構築パラメータ。タイトルは UTF-8 で渡す (内部で wide 変換)。
    struct WindowDesc
    {
        std::string title = "NS";
        int width = 1280;
        int height = 720;
    };

    /// Win32 ウィンドウのラッパ。
    /// 単一インスタンス前提 (HWND 所有権を固定するためコピー/ムーブ禁止)。
    /// 公開ヘッダから <windows.h> / HWND は露出させない (Graphics 層は NativeHandle() を reinterpret_cast)。
    class Window
    {
    public:
        /// 内部実装。定義は detail/win32_window.h にある。
        /// 公開しているのは WndProc などの自由関数からアクセス可能にするためで、外部から触らないこと。
        struct Impl;

        explicit Window(const WindowDesc& desc);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        /// 1 フレーム頭で呼ぶ。PeekMessageW(PM_REMOVE) で非ブロッキング処理。
        /// WM_QUIT を受信したら ShouldClose() が true になる。
        void PollMessages();

        [[nodiscard]] bool ShouldClose() const noexcept;

        [[nodiscard]] int Width() const noexcept;
        [[nodiscard]] int Height() const noexcept;

        /// HWND を void* で公開。Graphics 層は reinterpret_cast<HWND> で取り出す。
        [[nodiscard]] void* NativeHandle() const noexcept;

        /// UTF-8 入力でタイトル変更。
        void SetTitle(std::string_view utf8Title);

        /// PostQuitMessage 経由で次回 PollMessages 後に ShouldClose() を true にする。
        void RequestClose() noexcept;

        /// リサイズ通知 (WM_SIZE)。最小化中は呼ばれない。
        void SetResizeCallback(std::function<void(int width, int height)> cb);

        /// ×ボタン等で閉じる要求 (WM_CLOSE) を受け取った時に呼ばれる。
        /// callback 内で RequestClose() を呼ばないと閉じない (拒否可能)。
        void SetCloseCallback(std::function<void()> cb);

    private:
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace ns::platform
