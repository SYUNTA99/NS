#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace NS::OS
{
    class Input;

    //! @brief ウィンドウ作成時の初期パラメータ
    struct WindowDesc
    {
        //! ウィンドウのタイトル
        std::string title = "NS";

        //! クライアント領域のサイズ
        NS::Size2D size{1920, 1080};

        //! ウィンドウ生成時の初期表示フラグ
        bool visible = true;
    };

    //! @brief OS ウィンドウ
    //! @details 単一インスタンス。二重に作ると致命ログを出す
    class Window : public NS::NonCopyable
    {
    public:
        //! @brief OS のメッセージをそのまま受け取るコールバック型
        //! @note ImGui へのイベント転送に使う
        using MessageHook = std::function<void(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)>;

        explicit Window(const WindowDesc& desc);
        ~Window();

        //! @brief ウィンドウの構築が成功したかどうかを返す
        [[nodiscard]] bool IsValid() const noexcept;

        //! @brief 毎フレーム呼び出し、OSのメッセージイベントを処理する
        void PollMessages() noexcept;

        //! @brief ウィンドウを閉じる要求が出ているかを返す
        [[nodiscard]] bool ShouldClose() const noexcept;

        //! @brief 現在のクライアント領域のサイズを取得する
        [[nodiscard]] NS::Size2D Size() const noexcept;

        //! @brief ウィンドウハンドルを返す。構築に失敗した時は nullptr
        [[nodiscard]] HWND NativeHandle() const noexcept;

        //! @brief ウィンドウのタイトルを変更する
        void SetTitle(std::string_view utf8Title) noexcept;

        //! @brief クライアント領域におけるマウスカーソルの表示・非表示を切り替える
        void SetCursorVisible(bool visible) noexcept;

        //! @brief 現在のマウスカーソルの表示状態を取得する
        [[nodiscard]] bool IsCursorVisible() const noexcept;

        //! @brief カーソルの固定を切り替える
        //! @note 固定中は PollMessages が毎フレームカーソルを固定点へ戻す。フォーカスを失っている間は戻さない
        void SetCursorLocked(bool locked) noexcept;

        //! @brief 現在のカーソル固定状態を取得する
        [[nodiscard]] bool IsCursorLocked() const noexcept;

        //! @brief カーソル固定の戻し先をクライアント座標で設定する
        //! @note 未設定の間はクライアント領域の中央へ戻す
        void SetCursorLockPoint(int clientX, int clientY) noexcept;

        //! @brief プログラム側からウィンドウを閉じる要求を発行する
        void RequestClose() noexcept;

        //! @brief リサイズ時に呼び出されるコールバックを設定する
        //! @note 最小化時はコールバックの呼び出しがスキップされる
        void SetResizeCallback(std::function<void(NS::Size2D)> cb);

        //! @brief ウィンドウの閉じる要求を受け取った際のコールバックを設定する
        //! @note 独自の終了処理や、終了をキャンセルする処理をここで挟むことができる
        void SetCloseCallback(std::function<void()> cb);

        //! @brief ウィンドウへの入力イベントの転送先を設定する
        void AttachInput(Input* input) noexcept;

        //! @brief ネイティブメッセージのフックコールバックを設定する
        void SetMessageHook(MessageHook hook);

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        HWND m_hwnd = nullptr;
        ATOM m_classAtom = 0;
        HINSTANCE m_hInstance = nullptr;

        NS::Size2D m_size{0, 0};
        bool m_shouldClose = false; //!< WM_QUIT を受けたか

        std::function<void(NS::Size2D)> m_onResize;
        std::function<void()> m_onClose;

        Input* m_input = nullptr; //!< 入力の転送先。持ち主ではない
        MessageHook m_messageHook;

        // false の間はクライアント領域のカーソルを消す。WM_SETCURSOR がこの値を見て適用する
        bool m_cursorVisible = true;

        bool m_cursorLocked = false;
        // 一度も前に出ない窓には WM_SETFOCUS が来ない。作成時に実際の状態を書く
        bool m_hasFocus = false;
        std::optional<POINT> m_lockPoint; //!< 無い時はクライアント領域の中央
    };

} // namespace NS::OS
