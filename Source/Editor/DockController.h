#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <string>

namespace NS
{
    namespace Editor
    {
        //! ドック配置とパネルの全面化を管理する。パネルの内容は呼び手が描く
        class DockController : public NonCopyable
        {
        public:
            enum class Placement
            {
                Tab,
                Left,
                Right,
                Above,
                Below,
                Floating
            };

            //! @brief ツールバー帯の下にドックホストを 1 枚置く
            //! @param[in] topOffset ツールバー帯の高さ分だけ上端を空ける
            //! @details 非表示中は keepAliveOnly で配置を維持する。配置が無い場合とリセット時だけ初期配置を組む
            void RenderDockSpaceHost(float topOffset, bool keepAliveOnly = false) noexcept;

            void RenderWindowMenu() noexcept;
            //! パネルを配置先へ合流・分割する。独立化では target を使わない。未発行のパネルは動かさない
            void MovePanel(const char* panel, const char* target, Placement placement) noexcept;
            //! 次にドックを表示する時に初期配置へ戻す
            void ResetLayout() noexcept;
            //! 最大化中も元の配置を保存する。書き込めなければ false
            [[nodiscard]] bool SaveLayout(const char* path) const;
            //! 保存した配置を復元する。読み込めない場合は配置を変えず false
            [[nodiscard]] bool LoadLayout(const char* path);

            //! ポインター下のパネルを Shift+Space で全面化し、全面化中は元へ戻す
            //! shortcutsBlocked は ResolveInputOwnership の答え。真の間はキーを読まない
            void RenderMaximizeButton(bool shortcutsBlocked) noexcept;

            //! レイアウトを組み直した次フレームで前面タブを確定させる
            void TickTabFocus() noexcept;

            [[nodiscard]] bool IsMaximized() const noexcept { return !m_maximizedPanel.empty(); }

            //! 全面化中のパネル名。通常時は空
            [[nodiscard]] const std::string& MaximizedPanel() const noexcept { return m_maximizedPanel; }

            //! 全面化に入る直前のウィンドウ配置を復元する
            void ClearMaximized() noexcept { ExitMaximize(); }

            //! 現在のレイアウトを控えて name を全面化する
            void EnterMaximize(const char* name) noexcept;

            //! 控えたレイアウトへ戻し全面化を解く
            void ExitMaximize() noexcept;

        private:
            void RenderPanelMenu(const char* panel) noexcept;

            std::string m_maximizedPanel; // 全面化中のパネル名。空なら通常のドック表示
            std::string m_savedImGuiIni;  // 全面化に入る直前のドックレイアウト。復元で書き戻す
            int m_dockFocusPending = 0;   // 残りフレーム数。0 になったフレームで前面タブを確定させる
            bool m_resetPending = false;
            const char* m_normalIniFilename = nullptr;
            std::string m_layoutStatus;
        };
    } // namespace Editor
} // namespace NS
