#include "Game/Editor/EditorMode.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"
#include "Game/LevelEditorController.h"
#include "Game/LevelPlayScene.h"

#include <gtest/gtest.h>

/// Application 依存のない LevelPlayScene + LevelEditorController の組を相手に、
/// mode toggle の enum / EditorMode / PlayMode の active 切替 / Undo 履歴保持を検証する
/// Setup / OnStart は Application::Get() を要求するため呼ばない (player 等は nullptr のまま)

TEST(ModeToggle, InitialModeIsEdit)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
}

TEST(ModeToggle, EnterPlayDeactivatesEditor)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
    EXPECT_FALSE(editor.Editor().IsActive());
    EXPECT_TRUE(scene.PlayModeSub().IsActive());
}

TEST(ModeToggle, EnterEditReactivatesEditor)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
    EXPECT_TRUE(editor.Editor().IsActive());
    EXPECT_FALSE(scene.PlayModeSub().IsActive());
}

TEST(ModeToggle, RedundantEnterIsNoOp)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
    editor.EnterPlay();
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
}

TEST(ModeToggle, EditorStateIsPreservedAcrossToggle_PMODE_03)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    editor.Editor().SetLevel(&scene.Level());
    editor.Editor().PlaceUnderCursorProgrammatic(5, 0, 3);
    const auto undoSizeBefore = editor.Editor().Undo().UndoSize();
    ASSERT_GE(undoSizeBefore, 1u);
    const auto objectsBefore = scene.Level().objects.size();

    editor.EnterPlay();
    editor.EnterEdit();

    EXPECT_EQ(editor.Editor().Undo().UndoSize(), undoSizeBefore);
    EXPECT_EQ(scene.Level().objects.size(), objectsBefore);
}

TEST(ModeToggle, SingleFrameFlipIsCompletePMODE_01)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Play);
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
}

TEST(ModeToggle, QuitToEditWhilePausedResetsPausedFlag)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    scene.Play().paused = true;
    editor.EnterEdit();
    EXPECT_FALSE(scene.Play().paused);

    editor.EnterPlay();
    EXPECT_FALSE(scene.Play().paused);
}

TEST(ModeToggle, EnterPlayInitializesPlayStateAtSpawn)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    scene.Level().spawnX = 7;
    scene.Level().spawnY = 2;
    scene.Level().spawnZ = -4;
    editor.EnterPlay();

    EXPECT_NEAR(scene.Play().playerPosition.x, 7.0f, 1e-4f);
    // y は spawn セル底面 + (capsule halfHeight + radius) + 1cm lift = spawnY + 0.41
    EXPECT_NEAR(scene.Play().playerPosition.y, 2.41f, 1e-3f);
    EXPECT_NEAR(scene.Play().playerPosition.z, -4.0f, 1e-4f);
}
