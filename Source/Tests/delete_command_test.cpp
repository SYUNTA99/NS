#include "Editor/Undo/DeleteCommand.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::GameCore::Level;

TEST(DeleteCommandTest, DoRemovesExistingBlock)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, 2));
    EditorNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    EXPECT_TRUE(lv.objects.empty());
}

TEST(DeleteCommandTest, UndoRestoresOriginalEntry)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, 2));
    const auto before = lv.ComputeCrc32();
    EditorNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    cmd.Undo(lv);
    ASSERT_EQ(lv.objects.size(), 1u);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 2, 0, 4);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::ObjectCellX(lv.objects[idx]), 2);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 2u);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}

TEST(DeleteCommandTest, NonExistentCellIsNoOp)
{
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();
    EditorNs::DeleteCommand cmd(99, 99, 99);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.objects.empty());
}
