#include "Editor/PlayControls.h"

#include <optional>

namespace NS::Editor
{
    CenterTab LiveCenterTab(bool playMode) noexcept
    {
        if (playMode)
        {
            return CenterTab::Game;
        }
        return CenterTab::Scene;
    }

    PlayToolbarState MakePlayToolbarState(PlayModeSnapshot snapshot) noexcept
    {
        PlayToolbarState state{};
        state.playActive = snapshot.playMode;
        if (!snapshot.playMode)
        {
            // 編集中は paused が残っていても pause / コマ送りを落とした表示にする
            return state;
        }
        state.pauseDown = snapshot.paused;
        state.pauseEnabled = true;
        state.stepEnabled = true;
        return state;
    }

    std::optional<CenterTab> TabFocusOnModeChange(ModeTransition transition) noexcept
    {
        if (transition.wasPlayMode == transition.playMode)
        {
            return std::nullopt;
        }
        return LiveCenterTab(transition.playMode);
    }

    CenterPanelRole ResolveCenterPanelRole(CenterPanelQuery query) noexcept
    {
        if (query.tab == CenterTab::Game)
        {
            if (query.playMode)
            {
                return CenterPanelRole::LiveView;
            }
            return CenterPanelRole::Placeholder;
        }

        // Scene タブ
        if (!query.playMode)
        {
            return CenterPanelRole::LiveView;
        }
        if (query.otherDisplayed)
        {
            return CenterPanelRole::Placeholder;
        }
        return CenterPanelRole::FreeView;
    }
} // namespace NS::Editor
