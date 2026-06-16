#pragma once

/// @file Window.h
/// @brief NS::Platform::Window — Win32 ウィンドウのラッパ (単一インスタンス前提)
///
/// @details 公開ヘッダから `<windows.h>` / HWND は露出させない。 Graphics 層は
/// `NativeHandle()` を `reinterpret_cast<HWND>` で取り出す。 構築失敗時は
/// `IsValid() == false` を返し例外は投げない (詳細は `NS_LOG_ERROR` に出力)
/// 入力転送先 `Input*` は `AttachInput()` で非所有ポインタとして登録する

#include "Framework/Math/Math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

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

    /// Win32 ウィンドウのラッパ。単一インスタンス前提、コピー/ムーブ禁止
    class Window
    {
    public:
        /// 内部実装 (定義は detail/win32_window.h)。WndProc 等の自由関数からのアクセス用で外部から触らないこと
        struct Impl;

        /// 生 Win32 メッセージを WndProc 先頭で覗くフック。editor が ImGui へ転送する用途で登録する
        /// Platform 層を UI へ依存させないため型は void* / 整数で受け、解釈は登録側に委ねる
        using MessageHook =
            std::function<void(void* hwnd, std::uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam)>;

        explicit Window(const WindowDesc& desc);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        /// 構築成功判定。RegisterClassExW / CreateWindowExW が失敗した時は false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 1 フレーム頭で呼ぶ。WM_QUIT 受信で ShouldClose() が true になる
        void PollMessages() noexcept;

        [[nodiscard]] bool ShouldClose() const noexcept;

        [[nodiscard]] NS::Math::Size2D Size() const noexcept;

        /// HWND を void* で公開。Graphics 層は reinterpret_cast<HWND> で取り出す
        [[nodiscard]] void* NativeHandle() const noexcept;

        /// UTF-8 入力でタイトル変更
        void SetTitle(std::string_view utf8Title) noexcept;

        /// WM_CLOSE を投げて閉じ要求を出す (×ボタンと同じ経路)
        /// SetCloseCallback が登録されていればそこへ通知、未設定なら PostQuitMessage に落ちる
        void RequestClose() noexcept;

        /// リサイズ通知 (WM_SIZE)。最小化中は呼ばれない
        void SetResizeCallback(std::function<void(NS::Math::Size2D)> cb);

        /// WM_CLOSE を受け取った時に呼ばれる。callback 内で RequestClose() を呼ばないと閉じない (拒否可能)
        void SetCloseCallback(std::function<void()> cb);

        /// 入力転送先を設定する。非所有ポインタ、nullptr で解除
        void AttachInput(Input* input) noexcept;

        /// 生メッセージフックを設定する。WndProc 先頭で呼ばれる。nullptr で解除
        /// ゲーム入力のゲートは Input::UiWantsMouse / UiWantsKeyboard を見て判定する
        void SetMessageHook(MessageHook hook);

    private:
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace NS::Platform
