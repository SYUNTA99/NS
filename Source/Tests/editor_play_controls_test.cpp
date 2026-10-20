#include "Editor/PlayControls.h"

#include <gtest/gtest.h>

TEST(EditorPlayControls, EditShortcutsAreBlockedOnlyWhileTypingOperatingAnItemOrInAModal)
{
    for (const bool playMode : {false, true})
    {
        SCOPED_TRACE(playMode);

        NS::Editor::InputOwnerQuery typing{};
        typing.playMode = playMode;
        typing.uiWantsKeyboard = true;
        typing.textInput = true;
        typing.itemActive = true;
        EXPECT_TRUE(NS::Editor::ResolveInputOwnership(typing).editShortcutsBlocked);

        NS::Editor::InputOwnerQuery operating{};
        operating.playMode = playMode;
        operating.uiWantsKeyboard = true;
        operating.itemActive = true;
        EXPECT_TRUE(NS::Editor::ResolveInputOwnership(operating).editShortcutsBlocked);

        // 曲線の編集のモーダルでは F が曲線を収める。選んだアクターへ寄る F を重ねない
        NS::Editor::InputOwnerQuery modal{};
        modal.playMode = playMode;
        modal.uiWantsKeyboard = true;
        modal.modalOpen = true;
        EXPECT_TRUE(NS::Editor::ResolveInputOwnership(modal).editShortcutsBlocked);

        // 一覧やインスペクタに焦点があるだけの時は、ImGui がキーボードを欲しがっても止めない
        NS::Editor::InputOwnerQuery panelFocused{};
        panelFocused.playMode = playMode;
        panelFocused.uiWantsKeyboard = true;
        EXPECT_FALSE(NS::Editor::ResolveInputOwnership(panelFocused).editShortcutsBlocked);

        NS::Editor::InputOwnerQuery centerFocused{};
        centerFocused.playMode = playMode;
        centerFocused.uiWantsKeyboard = true;
        centerFocused.focusedPanel = NS::Editor::CenterTab::Scene;
        EXPECT_FALSE(NS::Editor::ResolveInputOwnership(centerFocused).editShortcutsBlocked);

        const NS::Editor::InputOwnerQuery idle{.playMode = playMode};
        EXPECT_FALSE(NS::Editor::ResolveInputOwnership(idle).editShortcutsBlocked);
    }
}
