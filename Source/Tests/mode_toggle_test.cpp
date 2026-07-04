#include "Editor/EditorMode.h"
#include "Editor/LevelEditorController.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"
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
    EXPECT_TRUE(scene.Director().Flow().PlayModeSub().IsActive());
}

TEST(ModeToggle, EnterEditReactivatesEditor)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    editor.EnterEdit();
    EXPECT_EQ(editor.CurrentMode(), LevelEditorController::Mode::Edit);
    EXPECT_TRUE(editor.Editor().IsActive());
    EXPECT_FALSE(scene.Director().Flow().PlayModeSub().IsActive());
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
    auto& flow = scene.Director().Flow();
    editor.EnterPlay();
    flow.Play().paused = true;
    editor.EnterEdit();
    EXPECT_FALSE(flow.Play().paused);

    editor.EnterPlay();
    EXPECT_FALSE(flow.Play().paused);
}

TEST(ModeToggle, EnterPlayInitializesPlayStateAtPlayerObject)
{
    LevelPlayScene scene;
    LevelEditorController editor(&scene);
    auto& flow = scene.Director().Flow();
    scene.Level().objects.push_back(
        NS::Game::Level::MakePlayerObject(NS::Math::Vector3{7.0f, 2.0f, -4.0f}, NS::Math::Quaternion{}));
    editor.EnterPlay();

    EXPECT_NEAR(flow.Play().playerPosition.x, 7.0f, 1e-4f);
    // プレイヤー実体の位置は capsule 中心 world 位置そのものなので player はその座標へ正確に置かれる
    EXPECT_NEAR(flow.Play().playerPosition.y, 2.0f, 1e-4f);
    EXPECT_NEAR(flow.Play().playerPosition.z, -4.0f, 1e-4f);
}
