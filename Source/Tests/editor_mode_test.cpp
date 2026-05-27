#include "Game/Editor/BlockRegistry.h"
#include "Game/Editor/EditorMode.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Game::Editor;
namespace LevelNs = NS::Game::Level;

TEST(EditorMode, ProgrammaticPlaceAddsBlock)
{
    LevelNs::LevelData lv;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.PlaceUnderCursorProgrammatic(5, 0, 3);

    ASSERT_EQ(lv.blocks.size(), 1u);
    EXPECT_EQ(lv.blocks[0].x, 5);
    EXPECT_EQ(lv.blocks[0].y, 0);
    EXPECT_EQ(lv.blocks[0].z, 3);
    EXPECT_EQ(lv.blocks[0].blockId, EditorNs::kBlockIdSolid);
}

TEST(EditorMode, ProgrammaticDeleteRemovesBlock)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({2, 0, 4, EditorNs::kBlockIdSolid, 0, 0});
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.DeleteAtProgrammatic(2, 0, 4);

    EXPECT_TRUE(lv.blocks.empty());
}

TEST(EditorMode, ProgrammaticRotateCycles)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({0, 0, 0, EditorNs::kBlockIdSolid, 0, 0});
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(lv.blocks[0].rotation, 1);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(lv.blocks[0].rotation, 2);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(lv.blocks[0].rotation, 3);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(lv.blocks[0].rotation, 0);
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
    EXPECT_TRUE(lv.blocks.empty());
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
    lv.blocks.push_back({0, 0, 0, EditorNs::kBlockIdSolid, 0, 0});
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(lv.blocks[0].rotation, 1);
    EXPECT_TRUE(editor.IsLevelDirty());

    ASSERT_TRUE(editor.Undo().Undo(lv));
    EXPECT_EQ(lv.blocks[0].rotation, 0);
}
