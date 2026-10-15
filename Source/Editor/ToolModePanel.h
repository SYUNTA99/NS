#pragma once

#include "NSlib/Core/NonCopyable.h"

class LevelEditorController;

namespace NS
{
    namespace Editor
    {
        //! @brief Build / Object の編集モードを切り替え、主要ショートカットを一覧するパネル
        class ToolModePanel : public NS::NonCopyable
        {
        public:
            //! Edit Mode パネルを 1 枚描く。エディタを外したビルドでは何もしない
            void Render(LevelEditorController& editor) noexcept;
        };
    } // namespace Editor
} // namespace NS
