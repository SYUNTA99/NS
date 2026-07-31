#include "Editor/PlayControls.h"

#include <gtest/gtest.h>

using NS::Editor::CenterPanelRole;
using NS::Editor::CenterTab;
using NS::Editor::LiveCenterTab;
using NS::Editor::MakePlayToolbarState;
using NS::Editor::ResolveCenterPanelRole;
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
    const auto state = MakePlayToolbarState({.playMode = false, .paused = false});
    EXPECT_FALSE(state.playActive);
    EXPECT_FALSE(state.pauseDown);
    EXPECT_FALSE(state.pauseEnabled);
    EXPECT_FALSE(state.stepEnabled);
}

TEST(PlayControlsTest, ToolbarIgnoresPausedWhileEditing)
{
    // 編集中は paused が残っていても一時停止表示を出さない
    const auto state = MakePlayToolbarState({.playMode = false, .paused = true});
    EXPECT_FALSE(state.playActive);
    EXPECT_FALSE(state.pauseDown);
    EXPECT_FALSE(state.pauseEnabled);
    EXPECT_FALSE(state.stepEnabled);
}

TEST(PlayControlsTest, ToolbarWhilePlaying)
{
    const auto state = MakePlayToolbarState({.playMode = true, .paused = false});
    EXPECT_TRUE(state.playActive);
    EXPECT_FALSE(state.pauseDown);
    EXPECT_TRUE(state.pauseEnabled);
    EXPECT_TRUE(state.stepEnabled);
}

TEST(PlayControlsTest, ToolbarWhilePaused)
{
    const auto state = MakePlayToolbarState({.playMode = true, .paused = true});
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
