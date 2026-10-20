#include "Editor/EditorMode.h"
#include "Editor/LevelFilePaths.h"
#include "Game/Game.h"

#include <gtest/gtest.h>

#include <string>

// 起動シーンの道の持ち主は Game だけ。エディタのレベルの名前はそこから引く
TEST(EditorLevelOpen, StartSceneLevelNameComesFromTheGameStartScenePath)
{
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath(::Game::StartScenePath()), "Scenes/new_scene");
}

// 名前の無いまま終了の保存をすると、起動シーンへ書き戻す。書き先の名前も Game の道から引く
TEST(EditorLevelOpen, QuitSaveWithoutALevelNameTargetsTheStartScene)
{
    NS::Editor::EditorMode editor;

    EXPECT_EQ(editor.QuitSaveLevelName(), NS::Editor::LevelNameOfScenePath(::Game::StartScenePath()));
}

// Assets の外・拡張子の違い・使えない文字は名前にしない
TEST(EditorLevelOpen, LevelNameOfScenePathRejectsPathsOutsideAssetsOrNotScenes)
{
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/Scenes/a.SCENE"), "Scenes/a");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Scenes/a.scene"), "");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/Scenes/a.mat"), "");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/Scenes/a"), "");
    EXPECT_EQ(NS::Editor::LevelNameOfScenePath("Assets/../a.scene"), "");
}
