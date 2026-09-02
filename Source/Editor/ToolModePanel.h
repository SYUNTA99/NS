#pragma once

#include "Runtime/Core/NonCopyable.h"

class LevelEditorController;

namespace NS::Editor
{
    //! @brief Build / Object の編集モードを切り替え、 主要ショートカットを一覧するパネル
    class ToolModePanel : public NS::Core::NonCopyable
    {
    public:
        //! Edit Mode パネルを 1 枚描く。 エディタを外したビルドでは何もしない
        void Render(LevelEditorController& editor) noexcept;
    };
} // namespace NS::Editor
