#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace NS::Platform
{
    class Input;

    //! @brief ウィンドウ作成時の初期パラメータ
    struct WindowDesc
    {
        //! ウィンドウのタイトル
        std::string title = "NS";

        //! クライアント領域のサイズ
        NS::Core::Size2D size{1280, 720};

        //! ウィンドウ生成時の初期表示フラグ
        bool visible = true;
    };

    //! @brief OS ウィンドウ
    //! @details OS 固有の API を隠し、メッセージ処理とサイズ・カーソル状態の取得口をまとめる
    class Window : public NS::Core::NonCopyable
    {
    public:
        //! 内部実装用の不透明構造体
        struct Impl;

        //! @brief OS のネイティブメッセージをフックするためのコールバック型
        //! @note ImGui へのイベント転送に使う。プラットフォーム層を外部へ依存させないための口
        using MessageHook =
            std::function<void(void* hwnd, std::uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam)>;

        explicit Window(const WindowDesc& desc);
        ~Window();

        //! @brief ウィンドウの構築が成功したかどうかを返す
        [[nodiscard]] bool IsValid() const noexcept;

        //! @brief 毎フレーム呼び出し、OSのメッセージイベントを処理する
        void PollMessages() noexcept;

        //! @brief ウィンドウを閉じる要求が出ているかを返す
        [[nodiscard]] bool ShouldClose() const noexcept;

        //! @brief 現在のクライアント領域のサイズを取得する
        [[nodiscard]] NS::Core::Size2D Size() const noexcept;

        //! @brief ネイティブのウィンドウハンドルを取得する
        //! @note Graphics 層が HWND へキャストして使う
        [[nodiscard]] void* NativeHandle() const noexcept;

        //! @brief ウィンドウのタイトルを変更する
        void SetTitle(std::string_view utf8Title) noexcept;

        //! @brief クライアント領域におけるマウスカーソルの表示・非表示を切り替える
        void SetCursorVisible(bool visible) noexcept;

        //! @brief 現在のマウスカーソルの表示状態を取得する
        [[nodiscard]] bool IsCursorVisible() const noexcept;

        //! @brief プログラム側からウィンドウを閉じる要求を発行する
        void RequestClose() noexcept;

        //! @brief リサイズ時に呼び出されるコールバックを設定する
        //! @note 最小化時はコールバックの呼び出しがスキップされる
        void SetResizeCallback(std::function<void(NS::Core::Size2D)> cb);

        //! @brief ウィンドウの閉じる要求を受け取った際のコールバックを設定する
        //! @note 独自の終了処理や、終了をキャンセルする処理をここで挟むことができる
        void SetCloseCallback(std::function<void()> cb);

        //! @brief ウィンドウへの入力イベントの転送先を設定する
        void AttachInput(Input* input) noexcept;

        //! @brief ネイティブメッセージのフックコールバックを設定する
        void SetMessageHook(MessageHook hook);

    private:
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace NS::Platform