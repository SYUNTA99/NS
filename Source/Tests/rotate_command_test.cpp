#include "Game/Level/LevelData.h"
#include "Game/Undo/RotateCommand.h"

#include <gtest/gtest.h>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

TEST(RotateCommandTest, DoIncrementsRotation)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({0, 0, 0, 1, 0, 0});
    UndoNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(lv);
    EXPECT_EQ(lv.blocks[0].rotation, 1u);
}

TEST(RotateCommandTest, FourDoesCycleBackToZero)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({0, 0, 0, 1, 0, 0});
    UndoNs::RotateCommand cmd1(0, 0, 0, +1);
    UndoNs::RotateCommand cmd2(0, 0, 0, +1);
    UndoNs::RotateCommand cmd3(0, 0, 0, +1);
    UndoNs::RotateCommand cmd4(0, 0, 0, +1);
    cmd1.Do(lv);
    cmd2.Do(lv);
    cmd3.Do(lv);
    cmd4.Do(lv);
    EXPECT_EQ(lv.blocks[0].rotation, 0u);
}

TEST(RotateCommandTest, NegativeDeltaWrapsToThree)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({0, 0, 0, 1, 0, 0});
    UndoNs::RotateCommand cmd(0, 0, 0, -1);
    cmd.Do(lv);
    EXPECT_EQ(lv.blocks[0].rotation, 3u);
}

TEST(RotateCommandTest, UndoRestoresPreviousRotation)
{
    LevelNs::LevelData lv;
    lv.blocks.push_back({0, 0, 0, 1, 2, 0});
    UndoNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(lv);
    EXPECT_EQ(lv.blocks[0].rotation, 3u);
    cmd.Undo(lv);
    EXPECT_EQ(lv.blocks[0].rotation, 2u);
}

TEST(RotateCommandTest, NonExistentCellIsNoOp)
{
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();
    UndoNs::RotateCommand cmd(7, 7, 7, +1);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}
