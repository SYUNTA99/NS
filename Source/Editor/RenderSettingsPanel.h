#pragma once

#include "Runtime/Core/NonCopyable.h"

class LevelEditorController;

namespace NS::Editor
{
    //! @brief 描画設定の解決結果と各項目の出所を出し、 シーン環境とテーマ雛形を編集するパネル
    class RenderSettingsPanel : public NS::Core::NonCopyable
    {
    public:
        void Render(LevelEditorController& editor) noexcept;
    };
} // namespace NS::Editor
