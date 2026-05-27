#include "Game/Level/LevelData.h"
#include "Game/Undo/PlaceCommand.h"

#include <gtest/gtest.h>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

TEST(PlaceCommandTest, DoAddsBlockEntry)
{
    LevelNs::LevelData lv;
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(lv);
    ASSERT_EQ(lv.blocks.size(), 1u);
    EXPECT_EQ(lv.blocks[0].x, 5);
    EXPECT_EQ(lv.blocks[0].y, 0);
    EXPECT_EQ(lv.blocks[0].z, 3);
    EXPECT_EQ(lv.blocks[0].blockId, 10u);
    EXPECT_EQ(lv.blocks[0].rotation, 1u);
}

TEST(PlaceCommandTest, UndoRestoresEmptyState)
{
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.blocks.empty());
}

TEST(PlaceCommandTest, ReplaceExistingBlockPreservesUndoRestore)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({5, 0, 3, 20, 2, 0});
    const auto before = lv.ComputeCrc32();
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(lv);
    EXPECT_EQ(lv.blocks[0].blockId, 10u);
    EXPECT_EQ(lv.blocks[0].rotation, 1u);
    cmd.Undo(lv);
    EXPECT_EQ(lv.blocks[0].blockId, 20u);
    EXPECT_EQ(lv.blocks[0].rotation, 2u);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}

TEST(PlaceCommandTest, RotationIsMaskedToTwoBits)
{
    LevelNs::LevelData lv;
    UndoNs::PlaceCommand cmd(0, 0, 0, 1, 5); // 5 & 3 == 1
    cmd.Do(lv);
    EXPECT_EQ(lv.blocks[0].rotation, 1u);
}
