#include "Editor/EditorMode.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

TEST(EditorMode, ProgrammaticPlaceAddsBlock)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetEditIds(&ids, &next);

    editor.PlaceUnderCursorProgrammatic(5, 0, 3);

    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(ids.size(), lv.objects.size());
    const auto idx = LevelNs::FindGridObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::ObjectCellX(lv.objects[idx]), 5);
    EXPECT_EQ(LevelNs::ObjectCellY(lv.objects[idx]), 0);
    EXPECT_EQ(LevelNs::ObjectCellZ(lv.objects[idx]), 3);
    EXPECT_TRUE(NS::Game::Blocks::IsGridSolidObject(lv.objects[idx]));
}

TEST(EditorMode, ProgrammaticDeleteRemovesBlock)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, 0));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetEditIds(&ids, &next);

    editor.DeleteAtProgrammatic(2, 0, 4);

    EXPECT_TRUE(lv.objects.empty());
    EXPECT_TRUE(ids.empty());
}

TEST(EditorMode, ProgrammaticRotateCycles)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetEditIds(&ids, &next);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 1);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 2);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 3);
    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 0);
}

TEST(EditorMode, UndoStackIntegration)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetEditIds(&ids, &next);

    const auto crc0 = lv.ComputeCrc32();
    editor.PlaceUnderCursorProgrammatic(0, 0, 0);
    ASSERT_EQ(editor.Undo().UndoSize(), 1u);

    LevelNs::EditTarget target{lv, ids, next};
    ASSERT_TRUE(editor.Undo().Undo(target));
    EXPECT_TRUE(lv.objects.empty());
    EXPECT_EQ(lv.ComputeCrc32(), crc0);
}

TEST(EditorMode, LevelDirtyFlagSetByMutation)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetEditIds(&ids, &next);

    editor.ClearLevelDirty();
    EXPECT_FALSE(editor.IsLevelDirty());

    editor.PlaceUnderCursorProgrammatic(0, 0, 0);
    EXPECT_TRUE(editor.IsLevelDirty());

    editor.ClearLevelDirty();
    editor.DeleteAtProgrammatic(0, 0, 0);
    EXPECT_TRUE(editor.IsLevelDirty());
}

TEST(EditorMode, CellRotationViaProgrammaticOnExistingBlock)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    EditorNs::EditorMode editor;
    editor.SetLevel(&lv);
    editor.SetEditIds(&ids, &next);

    editor.RotateAtProgrammatic(0, 0, 0);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 1);
    EXPECT_TRUE(editor.IsLevelDirty());

    LevelNs::EditTarget target{lv, ids, next};
    ASSERT_TRUE(editor.Undo().Undo(target));
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[0]), 0);
}
