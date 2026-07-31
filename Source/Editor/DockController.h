#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <string>

namespace NS::Editor
{
    //! @brief ドックホストとパネル全面化を受け持つ。 レイアウトと全面化の状態を持ち、 controller には触れない
    //! @details 各パネルの中身は描かない。 ホストの設置・ 全面化の出入り・ タブ焦点の確定だけを担い、
    //! 全面化中にどのパネルを描くかは呼び出し側が MaximizedPanel() を見て振り分ける
    class DockController : public NS::Core::NonCopyable
    {
    public:
        //! @brief ツールバー帯の下にドックホストを 1 枚置く
        //! @param topOffset ツールバー帯の高さ分だけ上端を空ける
        //! @details ini に配置が無ければ既定レイアウトを組み、 次フレームで前面タブを確定させる
        void RenderDockSpaceHost(float topOffset) noexcept;

        //! 通常時はタブバー右端、 全面化中は右上の浮遊ボタンに全面化 / 復元ボタンを描く
        void RenderMaximizeButton() noexcept;

        //! レイアウトを組み直した次フレームで前面タブを確定させる
        void TickTabFocus() noexcept;

        //! 全面化中か
        [[nodiscard]] bool IsMaximized() const noexcept { return !m_maximizedPanel.empty(); }

        //! 全面化中のパネル名。 通常時は空
        [[nodiscard]] const std::string& MaximizedPanel() const noexcept { return m_maximizedPanel; }

        //! 未知名を掴んだ時の保険として状態だけ全面化を解く
        void ClearMaximized() noexcept { m_maximizedPanel.clear(); }

    private:
        //! 現在のレイアウトを控えて name を全面化する
        void EnterMaximize(const char* name) noexcept;

        //! 控えたレイアウトへ戻し全面化を解く
        void ExitMaximize() noexcept;

        std::string m_maximizedPanel; // 全面化中のパネル名。空なら通常のドック表示
        std::string m_savedImGuiIni;  // 全面化に入る直前のドックレイアウト。復元で書き戻す
        int m_dockFocusPending = 0;   // 残りフレーム数。0 になったフレームで前面タブを確定させる
    };
} // namespace NS::Editor
