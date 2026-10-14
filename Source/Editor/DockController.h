#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <string>

namespace NS::Editor
{
    //! ドック配置とパネルの全面化を管理する。パネルの内容は呼び手が描く
    class DockController : public NS::NonCopyable
    {
    public:
        //! @brief ツールバー帯の下にドックホストを 1 枚置く
        //! @param[in] topOffset ツールバー帯の高さ分だけ上端を空ける
        //! @details ini に配置が無ければ既定レイアウトを組み、次フレームで前面タブを確定させる
        void RenderDockSpaceHost(float topOffset) noexcept;

        //! 通常時はタブバー右端、全面化中は右上の浮遊ボタンに全面化 / 復元ボタンを描く
        void RenderMaximizeButton() noexcept;

        //! レイアウトを組み直した次フレームで前面タブを確定させる
        void TickTabFocus() noexcept;

        [[nodiscard]] bool IsMaximized() const noexcept { return !m_maximizedPanel.empty(); }

        //! 全面化中のパネル名。通常時は空
        [[nodiscard]] const std::string& MaximizedPanel() const noexcept { return m_maximizedPanel; }

        //! レイアウトを復元せず、全面化状態だけを解除する
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
