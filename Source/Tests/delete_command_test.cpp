#include "Game/Level/LevelData.h"
#include "Game/Undo/DeleteCommand.h"

#include <gtest/gtest.h>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

TEST(DeleteCommandTest, DoRemovesExistingBlock)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, 5, 2));
    UndoNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    EXPECT_TRUE(lv.objects.empty());
}

TEST(DeleteCommandTest, UndoRestoresOriginalEntry)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, 5, 2));
    const auto before = lv.ComputeCrc32();
    UndoNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    cmd.Undo(lv);
    ASSERT_EQ(lv.objects.size(), 1u);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 2, 0, 4);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::ObjectCellX(lv.objects[idx]), 2);
    EXPECT_EQ(lv.objects[idx].kind, 5u);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 2u);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}

TEST(DeleteCommandTest, NonExistentCellIsNoOp)
{
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();
    UndoNs::DeleteCommand cmd(99, 99, 99);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.objects.empty());
}
