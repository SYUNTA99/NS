#pragma once

#if NS_EDITOR_ENABLED
#include <imgui.h>

namespace NS::Editor
{
    //! メニューや右クリックなどのドロップダウンを白地・黒文字で統一するための色
    //! PushStyleColor(ImGuiCol_PopupBg, ...) と PushStyleColor(ImGuiCol_Text, ...) に渡す
    //! Push した色は各ウィンドウの End (EndMenu / EndPopup / EndCombo) より前に必ず Pop する
    inline const ImVec4 kPopupBgColor{0.96f, 0.96f, 0.96f, 1.0f};
    inline const ImVec4 kPopupTextColor{0.10f, 0.10f, 0.10f, 1.0f};
} // namespace NS::Editor
#endif
