#include "Game/Editor/EditorMode.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"
#include "Game/LevelEditorScene.h"

#include <gtest/gtest.h>

/// Application 依存のない default-constructed LevelEditorScene を相手に、
/// mode toggle の enum / EditorMode / PlayMode の active 切替 / Undo 履歴保持を検証する。
/// OnStart は Application::Get() を要求するため呼ばない (m_player 等は nullptr のまま)。

TEST(ModeToggle, InitialModeIsEdit)
{
    LevelEditorScene scene;
    EXPECT_EQ(scene.CurrentMode(), LevelEditorScene::Mode::Edit);
}

TEST(ModeToggle, EnterPlayDeactivatesEditor)
{
    LevelEditorScene scene;
    scene.EnterPlay();
    EXPECT_EQ(scene.CurrentMode(), LevelEditorScene::Mode::Play);
    EXPECT_FALSE(scene.Editor().IsActive());
    EXPECT_TRUE(scene.PlayModeSub().IsActive());
}

TEST(ModeToggle, EnterEditReactivatesEditor)
{
    LevelEditorScene scene;
    scene.EnterPlay();
    scene.EnterEdit();
    EXPECT_EQ(scene.CurrentMode(), LevelEditorScene::Mode::Edit);
    EXPECT_TRUE(scene.Editor().IsActive());
    EXPECT_FALSE(scene.PlayModeSub().IsActive());
}

TEST(ModeToggle, RedundantEnterIsNoOp)
{
    LevelEditorScene scene;
    scene.EnterEdit();
    EXPECT_EQ(scene.CurrentMode(), LevelEditorScene::Mode::Edit);
    scene.EnterPlay();
    scene.EnterPlay();
    EXPECT_EQ(scene.CurrentMode(), LevelEditorScene::Mode::Play);
}

TEST(ModeToggle, EditorStateIsPreservedAcrossToggle_PMODE_03)
{
    LevelEditorScene scene;
    scene.Editor().SetLevel(&scene.Level());
    scene.Editor().PlaceUnderCursorProgrammatic(5, 0, 3);
    const auto undoSizeBefore = scene.Editor().Undo().UndoSize();
    ASSERT_GE(undoSizeBefore, 1u);
    const auto blocksBefore = scene.Level().blocks.size();

    scene.EnterPlay();
    scene.EnterEdit();

    EXPECT_EQ(scene.Editor().Undo().UndoSize(), undoSizeBefore);
    EXPECT_EQ(scene.Level().blocks.size(), blocksBefore);
}

TEST(ModeToggle, SingleFrameFlipIsCompletePMODE_01)
{
    LevelEditorScene scene;
    scene.EnterPlay();
    EXPECT_EQ(scene.CurrentMode(), LevelEditorScene::Mode::Play);
    scene.EnterEdit();
    EXPECT_EQ(scene.CurrentMode(), LevelEditorScene::Mode::Edit);
}

TEST(ModeToggle, QuitToEditWhilePausedResetsPausedFlag)
{
    LevelEditorScene scene;
    scene.EnterPlay();
    scene.Play().paused = true;
    scene.EnterEdit();
    EXPECT_FALSE(scene.Play().paused);

    scene.EnterPlay();
    EXPECT_FALSE(scene.Play().paused);
}

TEST(ModeToggle, EnterPlayInitializesPlayStateAtSpawn)
{
    LevelEditorScene scene;
    scene.Level().spawnX = 7;
    scene.Level().spawnY = 2;
    scene.Level().spawnZ = -4;
    scene.EnterPlay();

    EXPECT_NEAR(scene.Play().playerPosition.x, 7.5f, 1e-4f);
    EXPECT_NEAR(scene.Play().playerPosition.y, 3.0f, 1e-4f);
    EXPECT_NEAR(scene.Play().playerPosition.z, -3.5f, 1e-4f);
}
