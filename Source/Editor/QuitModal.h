#pragma once

#include "Runtime/Core/NonCopyable.h"

class LevelEditorController;

namespace NS::Editor
{
    //! @brief 終了時に保存を確認するモーダル。 終了ガードと保存確認の状態を持つ
    class QuitModal : public NS::Core::NonCopyable
    {
    public:
        //! @brief 終了要求を受ける
        //! @return 確認済みならそのまま終了して良いので true、 未確認なら modal を開いて握りつぶし false
        bool RequestQuit() noexcept;

        //! 保存確認 modal を描く。 開いていなければ何もしない
        void Render(LevelEditorController& editor) noexcept;

    private:
        bool m_open = false;       // modal 表示中か
        bool m_confirmed = false;  // 終了を確定したか。 二度目の終了要求を素通しする
        bool m_saveFailed = false; // 直前の保存が失敗したか。 modal に赤字で出す
    };
} // namespace NS::Editor
