#include "Editor/PlayControls.h"

#include <gtest/gtest.h>

TEST(EditorPlayControls, EditShortcutsAreBlockedOnlyWhileTypingOperatingAnItemOrInAModal)
{
    for (const bool playMode : {false, true})
    {
        SCOPED_TRACE(playMode);

        NS::Ed::InputOwnerQuery typing{};
        typing.playMode = playMode;
        typing.uiWantsKeyboard = true;
        typing.textInput = true;
        typing.itemActive = true;
        EXPECT_TRUE(NS::Ed::ResolveInputOwnership(typing).editShortcutsBlocked);

        NS::Ed::InputOwnerQuery operating{};
        operating.playMode = playMode;
        operating.uiWantsKeyboard = true;
        operating.itemActive = true;
        EXPECT_TRUE(NS::Ed::ResolveInputOwnership(operating).editShortcutsBlocked);

        // 曲線の編集のモーダルでは F が曲線を収める。選んだアクターへ寄る F を重ねない
        NS::Ed::InputOwnerQuery modal{};
        modal.playMode = playMode;
        modal.uiWantsKeyboard = true;
        modal.modalOpen = true;
        EXPECT_TRUE(NS::Ed::ResolveInputOwnership(modal).editShortcutsBlocked);

        // 一覧やインスペクタに焦点があるだけの時は、ImGui がキーボードを欲しがっても止めない
        NS::Ed::InputOwnerQuery panelFocused{};
        panelFocused.playMode = playMode;
        panelFocused.uiWantsKeyboard = true;
        EXPECT_FALSE(NS::Ed::ResolveInputOwnership(panelFocused).editShortcutsBlocked);

        NS::Ed::InputOwnerQuery centerFocused{};
        centerFocused.playMode = playMode;
        centerFocused.uiWantsKeyboard = true;
        centerFocused.focusedPanel = NS::Ed::CenterTab::Scene;
        EXPECT_FALSE(NS::Ed::ResolveInputOwnership(centerFocused).editShortcutsBlocked);

        const NS::Ed::InputOwnerQuery idle{.playMode = playMode};
        EXPECT_FALSE(NS::Ed::ResolveInputOwnership(idle).editShortcutsBlocked);
    }
}
