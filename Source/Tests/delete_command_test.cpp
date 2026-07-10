#include "Editor/Undo/DeleteCommand.h"
#include "Game/Level/LevelObjects.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Scene;

TEST(DeleteCommandTest, DoRemovesExistingBlock)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(2, 0, 4, 2));
    EditorNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    EXPECT_TRUE(lv.objects.empty());
}

TEST(DeleteCommandTest, UndoRestoresOriginalEntry)
{
    SceneNs::SceneData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(2, 0, 4, 2));
    const auto before = lv.ComputeCrc32();
    EditorNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    cmd.Undo(lv);
    ASSERT_EQ(lv.objects.size(), 1u);
    const std::size_t idx = LevelNs::FindObjectAtCell(lv, 2, 0, 4);
    ASSERT_NE(idx, SceneNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::ObjectCellX(lv.objects[idx]), 2);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 2u);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}

TEST(DeleteCommandTest, NonExistentCellIsNoOp)
{
    SceneNs::SceneData lv;
    const auto before = lv.ComputeCrc32();
    EditorNs::DeleteCommand cmd(99, 99, 99);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.objects.empty());
}
