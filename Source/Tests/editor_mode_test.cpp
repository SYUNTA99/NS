#include "Game/Blocks/BlockRegistry.h"
#include "Editor/EditorMode.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

TEST(EditorMode, ProgrammaticPlaceAddsBlock)
{
    LevelNs::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.PlaceUnderCursorProgrammatic(5, 0, 3);

    ASSERT_EQ(lv.objects.size(), 1u);
    const auto idx = LevelNs::FindGridObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::ObjectCellX(lv.objects[idx]), 5);
    EXPECT_EQ(LevelNs::ObjectCellY(lv.objects[idx]), 0);
    EXPECT_EQ(LevelNs::ObjectCellZ(lv.objects[idx]), 3);
    EXPECT_EQ(lv.objects[idx].kind, NS::Game::Blocks::kBlockIdSolid);
}

TEST(EditorMode, ProgrammaticDeleteRemovesBlock)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, NS::Game::Blocks::kBlockIdSolid, 0));
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.DeleteAtProgrammatic(2, 0, 4);

    EXPECT_TRUE(lv.objects.empty());
}

TEST(EditorMode, ProgrammaticRotateCycles)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, NS::Game::Blocks::kBlockIdSolid, 0));
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 1);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 2);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 3);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 0);
}

TEST(EditorMode, ProgrammaticSpawnSetsCoordinates)
{
    LevelNs::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.SetSpawnAtProgrammatic(10, 2, -5);

    EXPECT_EQ(lv.spawnX, 10);
    EXPECT_EQ(lv.spawnY, 2);
    EXPECT_EQ(lv.spawnZ, -5);
}

TEST(EditorMode, UndoStackIntegration)
{
    LevelNs::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    const auto crc0 = lv.ComputeCrc32();
    editor.PlaceUnderCursorProgrammatic(0, 0, 0);
    ASSERT_EQ(editor.Undo().UndoSize(), 1u);

    ASSERT_TRUE(editor.Undo().Undo(lv));
    EXPECT_TRUE(lv.objects.empty());
    EXPECT_EQ(lv.ComputeCrc32(), crc0);
}

TEST(EditorMode, LevelDirtyFlagSetByMutation)
{
    LevelNs::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.ClearLevelDirty();
    EXPECT_FALSE(editor.IsLevelDirty());

    editor.PlaceUnderCursorProgrammatic(0, 0, 0);
    EXPECT_TRUE(editor.IsLevelDirty());

    editor.ClearLevelDirty();
    editor.DeleteAtProgrammatic(0, 0, 0);
    EXPECT_TRUE(editor.IsLevelDirty());

    editor.ClearLevelDirty();
    editor.SetSpawnAtProgrammatic(1, 2, 3);
    EXPECT_TRUE(editor.IsLevelDirty());
}

TEST(EditorMode, CellRotationViaProgrammaticOnExistingBlock)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, NS::Game::Blocks::kBlockIdSolid, 0));
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 1);
    EXPECT_TRUE(editor.IsLevelDirty());

    ASSERT_TRUE(editor.Undo().Undo(lv));
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 0);
}
