#pragma once

#include "NSlib/Core/NonCopyable.h"

class LevelEditorController;

namespace NS
{
    namespace Editor
    {
        class EditorMode;
        class ActionTimelinePanel;

        //! 終了の前に問う、保存していない変更があるか
        //! シーンは履歴の版、動作のタイムラインはパネルの未保存の記録で答える。終了の確認はこの問いだけを見る
        [[nodiscard]] bool HasUnsavedWork(const EditorMode& editor, const ActionTimelinePanel& timeline) noexcept;

        //! 終了の前の保存。シーンと、動作のタイムラインで保存していない .actor を書く
        //! 片方が書けなくても、もう片方は書く
        //! @return 両方とも書けた時だけ true
        [[nodiscard]] bool SaveAllForQuit(EditorMode& editor, ActionTimelinePanel& timeline) noexcept;

        //! 終了時に保存を確認するモーダル。終了ガードと保存確認の状態を持つ
        class QuitModal : public NonCopyable
        {
        public:
            //! 終了要求を受ける
            //! @return 確認済みならそのまま終了して良いので true、未確認なら modal を開いて握りつぶし false
            bool RequestQuit() noexcept;

            //! 保存確認 modal を描く。開いていなければ何もしない
            void Render(LevelEditorController& editor, ActionTimelinePanel& timeline) noexcept;

        private:
            bool m_open = false;
            bool m_confirmed = false;  // 終了を確定したか。二度目の終了要求を素通しする
            bool m_saveFailed = false; // 直前の保存が失敗したか。modal に赤字で出す
        };
    } // namespace Editor
} // namespace NS
