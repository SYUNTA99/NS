#include "Game/Level/LevelData.h"
#include "Game/Undo/DeleteCommand.h"

#include <gtest/gtest.h>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

TEST(DeleteCommandTest, DoRemovesExistingBlock)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({2, 0, 4, 5, 2, 0});
    UndoNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    EXPECT_TRUE(lv.blocks.empty());
}

TEST(DeleteCommandTest, UndoRestoresOriginalEntry)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({2, 0, 4, 5, 2, 0});
    const auto before = lv.ComputeCrc32();
    UndoNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(lv);
    cmd.Undo(lv);
    ASSERT_EQ(lv.blocks.size(), 1u);
    EXPECT_EQ(lv.blocks[0].x, 2);
    EXPECT_EQ(lv.blocks[0].blockId, 5u);
    EXPECT_EQ(lv.blocks[0].rotation, 2u);
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
    EXPECT_TRUE(lv.blocks.empty());
}
