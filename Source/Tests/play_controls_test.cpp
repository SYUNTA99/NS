#include "Editor/PlayControls.h"

#include <gtest/gtest.h>

using NS::Editor::CenterPanelRole;
using NS::Editor::CenterTab;
using NS::Editor::CursorAfterPauseToggle;
using NS::Editor::InputOwnership;
using NS::Editor::LiveCenterTab;
using NS::Editor::MakePlayToolbarState;
using NS::Editor::PlayCursor;
using NS::Editor::ResolveCenterPanelRole;
using NS::Editor::ResolveInputOwnership;
using NS::Editor::ShouldRecaptureCursor;
using NS::Editor::TabFocusOnModeChange;

TEST(PlayControlsTest, LiveTabIsSceneWhileEditing)
{
    EXPECT_EQ(LiveCenterTab(false), CenterTab::Scene);
}

TEST(PlayControlsTest, LiveTabIsGameWhilePlaying)
{
    EXPECT_EQ(LiveCenterTab(true), CenterTab::Game);
}

TEST(PlayControlsTest, ToolbarAllOffWhileEditing)
{
    const NS::Editor::PlayToolbarState state = MakePlayToolbarState({.playMode = false, .paused = false});
    EXPECT_FALSE(state.playActive);
    EXPECT_FALSE(state.pauseDown);
    EXPECT_FALSE(state.pauseEnabled);
    EXPECT_FALSE(state.stepEnabled);
}

TEST(PlayControlsTest, ToolbarIgnoresPausedWhileEditing)
{
    // 編集中は paused が残っていても一時停止表示を出さない
    const NS::Editor::PlayToolbarState state = MakePlayToolbarState({.playMode = false, .paused = true});
    EXPECT_FALSE(state.playActive);
    EXPECT_FALSE(state.pauseDown);
    EXPECT_FALSE(state.pauseEnabled);
    EXPECT_FALSE(state.stepEnabled);
}

TEST(PlayControlsTest, ToolbarWhilePlaying)
{
    const NS::Editor::PlayToolbarState state = MakePlayToolbarState({.playMode = true, .paused = false});
    EXPECT_TRUE(state.playActive);
    EXPECT_FALSE(state.pauseDown);
    EXPECT_TRUE(state.pauseEnabled);
    EXPECT_TRUE(state.stepEnabled);
}

TEST(PlayControlsTest, ToolbarWhilePaused)
{
    const NS::Editor::PlayToolbarState state = MakePlayToolbarState({.playMode = true, .paused = true});
    EXPECT_TRUE(state.playActive);
    EXPECT_TRUE(state.pauseDown);
    EXPECT_TRUE(state.pauseEnabled);
    EXPECT_TRUE(state.stepEnabled);
}

TEST(PlayControlsTest, NoFocusChangeWithoutModeChange)
{
    EXPECT_EQ(TabFocusOnModeChange({.wasPlayMode = false, .playMode = false}), std::nullopt);
    EXPECT_EQ(TabFocusOnModeChange({.wasPlayMode = true, .playMode = true}), std::nullopt);
}

TEST(PlayControlsTest, FocusGameOnPlayStart)
{
    EXPECT_EQ(TabFocusOnModeChange({.wasPlayMode = false, .playMode = true}), CenterTab::Game);
}

TEST(PlayControlsTest, FocusSceneOnPlayStop)
{
    EXPECT_EQ(TabFocusOnModeChange({.wasPlayMode = true, .playMode = false}), CenterTab::Scene);
}

TEST(PlayControlsTest, SceneIsLiveViewWhileEditingRegardlessOfOtherDisplayed)
{
    EXPECT_EQ(ResolveCenterPanelRole({.tab = CenterTab::Scene, .playMode = false, .otherDisplayed = false}),
              CenterPanelRole::LiveView);
    EXPECT_EQ(ResolveCenterPanelRole({.tab = CenterTab::Scene, .playMode = false, .otherDisplayed = true}),
              CenterPanelRole::LiveView);
}

TEST(PlayControlsTest, GameIsPlaceholderWhileEditingRegardlessOfOtherDisplayed)
{
    EXPECT_EQ(ResolveCenterPanelRole({.tab = CenterTab::Game, .playMode = false, .otherDisplayed = false}),
              CenterPanelRole::Placeholder);
    EXPECT_EQ(ResolveCenterPanelRole({.tab = CenterTab::Game, .playMode = false, .otherDisplayed = true}),
              CenterPanelRole::Placeholder);
}

TEST(PlayControlsTest, GameIsLiveViewWhilePlaying)
{
    EXPECT_EQ(ResolveCenterPanelRole({.tab = CenterTab::Game, .playMode = true, .otherDisplayed = false}),
              CenterPanelRole::LiveView);
}

TEST(PlayControlsTest, SceneIsFreeViewWhilePlayingAndGameHidden)
{
    EXPECT_EQ(ResolveCenterPanelRole({.tab = CenterTab::Scene, .playMode = true, .otherDisplayed = false}),
              CenterPanelRole::FreeView);
}

TEST(PlayControlsTest, SceneIsPlaceholderWhilePlayingAndGameDisplayed)
{
    EXPECT_EQ(ResolveCenterPanelRole({.tab = CenterTab::Scene, .playMode = true, .otherDisplayed = true}),
              CenterPanelRole::Placeholder);
}

// 編集中は前の式のまま。Scene の画像を掴んでいる間だけマウスを編集入力へ通し、左ボタンを別に渡すことはない
TEST(PlayControlsTest, EditModeKeepsTheSceneLatchRule)
{
    const InputOwnership latched =
        ResolveInputOwnership({.playMode = false, .uiWantsMouse = true, .editSceneLatched = true});
    EXPECT_FALSE(latched.uiMouse);
    EXPECT_FALSE(latched.uiKeyboard);
    EXPECT_FALSE(latched.leftButtonToGame);

    const InputOwnership focused = ResolveInputOwnership({.playMode = false,
                                                          .uiWantsMouse = true,
                                                          .uiWantsKeyboard = true,
                                                          .sceneLatched = true,
                                                          .focusedPanel = CenterTab::Scene});
    EXPECT_TRUE(focused.uiMouse);
    EXPECT_TRUE(focused.uiKeyboard);
    EXPECT_FALSE(focused.leftButtonToGame);
}

// プレイ中に Scene の画像の上に居る間は、動きと右・中・ホイールは自由視点に残し、左ボタンだけゲームへ渡す
TEST(PlayControlsTest, SceneImagePassesOnlyTheLeftButtonWhilePlaying)
{
    const InputOwnership onScene =
        ResolveInputOwnership({.playMode = true, .uiWantsMouse = true, .cursorReleased = true, .sceneLatched = true});
    EXPECT_TRUE(onScene.uiMouse);
    EXPECT_TRUE(onScene.leftButtonToGame);

    const InputOwnership elsewhere =
        ResolveInputOwnership({.playMode = true, .uiWantsMouse = true, .cursorReleased = true});
    EXPECT_TRUE(elsewhere.uiMouse);
    EXPECT_FALSE(elsewhere.leftButtonToGame);
}

// カーソルを固定して Game の画像を掴んでいる間はマウスを丸ごとゲームへ渡す。出している間は UI が持ち、
// 固定へ戻すクリックを溜めに数えない
TEST(PlayControlsTest, GameImageOwnsTheMouseOnlyWhileTheCursorIsCaptured)
{
    const InputOwnership captured =
        ResolveInputOwnership({.playMode = true, .uiWantsMouse = true, .gameLatched = true, .cursorReleased = false});
    EXPECT_FALSE(captured.uiMouse);

    const InputOwnership released =
        ResolveInputOwnership({.playMode = true, .uiWantsMouse = true, .gameLatched = true, .cursorReleased = true});
    EXPECT_TRUE(released.uiMouse);
    EXPECT_FALSE(released.leftButtonToGame);
}

// 焦点が Scene か Game のパネルにある間は、ImGui が押下でキーボードを欲しがってもゲームへ渡す
TEST(PlayControlsTest, CenterPanelFocusGivesTheKeyboardToTheGame)
{
    EXPECT_FALSE(
        ResolveInputOwnership(
            {.playMode = true, .uiWantsKeyboard = true, .cursorReleased = true, .focusedPanel = CenterTab::Scene})
            .uiKeyboard);
    EXPECT_FALSE(
        ResolveInputOwnership(
            {.playMode = true, .uiWantsKeyboard = true, .cursorReleased = true, .focusedPanel = CenterTab::Game})
            .uiKeyboard);
    // Inspector などの窓に焦点がある間は UI が持つ
    EXPECT_TRUE(ResolveInputOwnership({.playMode = true, .uiWantsKeyboard = true, .cursorReleased = true}).uiKeyboard);
}

// 文字の入力中と、Scene の見回しの右ドラッグ中は、焦点が中央パネルでも UI が持つ
TEST(PlayControlsTest, TextInputAndSceneLookKeepTheKeyboardInTheUi)
{
    EXPECT_TRUE(ResolveInputOwnership({.playMode = true,
                                       .uiWantsKeyboard = true,
                                       .textInput = true,
                                       .cursorReleased = true,
                                       .focusedPanel = CenterTab::Scene})
                    .uiKeyboard);
    EXPECT_TRUE(ResolveInputOwnership(
                    {.playMode = true, .cursorReleased = true, .sceneLooking = true, .focusedPanel = CenterTab::Scene})
                    .uiKeyboard);
}

// 止めたらカーソルを出す。再開は Game が前面の時だけ固定へ戻し、Scene を見ている時は出したままにする
TEST(PlayControlsTest, CursorAfterPauseToggleFollowsTheFrontPanel)
{
    EXPECT_EQ(CursorAfterPauseToggle({.paused = true, .gameViewInFront = true}), PlayCursor::Released);
    EXPECT_EQ(CursorAfterPauseToggle({.paused = false, .gameViewInFront = true}), PlayCursor::Captured);
    EXPECT_EQ(CursorAfterPauseToggle({.paused = false, .gameViewInFront = false}), PlayCursor::Released);
}

// カーソルを出しているプレイ中に Game の画像を左クリックした時だけ、固定へ戻す
TEST(PlayControlsTest, GameImageClickRecapturesTheReleasedCursor)
{
    EXPECT_TRUE(
        ShouldRecaptureCursor({.playMode = true, .paused = false, .cursorReleased = true, .gameImageClicked = true}));
    EXPECT_FALSE(
        ShouldRecaptureCursor({.playMode = true, .paused = false, .cursorReleased = true, .gameImageClicked = false}));
    // 止めている間は Inspector を触るためにカーソルを出しているので戻さない
    EXPECT_FALSE(
        ShouldRecaptureCursor({.playMode = true, .paused = true, .cursorReleased = true, .gameImageClicked = true}));
    EXPECT_FALSE(
        ShouldRecaptureCursor({.playMode = false, .paused = false, .cursorReleased = true, .gameImageClicked = true}));
}
