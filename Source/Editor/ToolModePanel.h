#pragma once

#include "NSlib/Core/NonCopyable.h"

class LevelEditorController;

namespace NS
{
    namespace Editor
    {
        // ステージ作成・選択変形・グリッド配置を切り替え、配置の設定を編集する。
        class ToolModePanel : public NonCopyable
        {
        public:
            //! Edit Mode パネルを 1 枚描く。エディタを外したビルドでは何もしない
            void Render(LevelEditorController& editor) noexcept;
        };
    } // namespace Editor
} // namespace NS
