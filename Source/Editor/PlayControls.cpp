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

    InputOwnership ResolveInputOwnership(InputOwnerQuery query) noexcept
    {
        InputOwnership owner{};
        if (!query.playMode)
        {
            // 編集中は Scene の画像を掴んでいる間だけマウスを編集入力へ通す
            owner.uiMouse = query.uiWantsMouse && !query.editSceneLatched;
            owner.uiKeyboard = query.uiWantsKeyboard || query.sceneLooking;
            return owner;
        }

        // マウスを丸ごとゲームへ渡すのは、カーソルを固定して Game の画像を掴んでいる間だけ
        // 出している間の Game の画像は固定へ戻すクリックを受けるので、溜めへ流さない
        const bool gameHoldsMouse = query.gameLatched && !query.cursorReleased;
        owner.uiMouse = query.uiWantsMouse && !gameHoldsMouse;

        // Scene の画像から渡すのは左ボタンだけ。動きと右ボタンを渡すと、ゲームのカメラが回って崖の手放しも起きる
        // 押す前のフレームから立てておく。立てるのが押したフレームの後だと、押下のメッセージを捨てた後になる
        owner.leftButtonToGame = query.sceneLatched;

        // ImGui は画像を押している間もキーボードを欲しがる。焦点が中央パネルなら歩けるようゲームへ渡す
        const bool centerFocused = query.focusedPanel.has_value() && !query.textInput;
        owner.uiKeyboard = query.sceneLooking || (query.uiWantsKeyboard && !gameHoldsMouse && !centerFocused);
        return owner;
    }

    PlayCursor CursorAfterPauseToggle(PauseToggleQuery query) noexcept
    {
        // 止めている間は Inspector を触れるよう出す
        if (query.paused)
        {
            return PlayCursor::Released;
        }
        // Scene を見ている時に固定すると、見えない Game の中心へカーソルが飛んでタブを押せなくなる
        if (!query.gameViewInFront)
        {
            return PlayCursor::Released;
        }
        return PlayCursor::Captured;
    }

    bool ShouldRecaptureCursor(RecaptureQuery query) noexcept
    {
        if (!query.playMode || query.paused)
        {
            return false;
        }
        return query.cursorReleased && query.gameImageClicked;
    }
} // namespace NS::Editor
