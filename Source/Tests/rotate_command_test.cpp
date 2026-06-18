#include "Game/Level/LevelData.h"
#include "Game/Undo/EditTarget.h"
#include "Game/Undo/RotateCommand.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

TEST(RotateCommandTest, DoIncrementsRotation)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    UndoNs::EditTarget t{lv, ids, next};
    UndoNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(t);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
}

TEST(RotateCommandTest, FourDoesCycleBackToZero)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    UndoNs::EditTarget t{lv, ids, next};
    UndoNs::RotateCommand cmd1(0, 0, 0, +1);
    UndoNs::RotateCommand cmd2(0, 0, 0, +1);
    UndoNs::RotateCommand cmd3(0, 0, 0, +1);
    UndoNs::RotateCommand cmd4(0, 0, 0, +1);
    cmd1.Do(t);
    cmd2.Do(t);
    cmd3.Do(t);
    cmd4.Do(t);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 0u);
}

TEST(RotateCommandTest, NegativeDeltaWrapsToThree)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    UndoNs::EditTarget t{lv, ids, next};
    UndoNs::RotateCommand cmd(0, 0, 0, -1);
    cmd.Do(t);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 3u);
}

TEST(RotateCommandTest, UndoRestoresPreviousRotation)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 2));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    UndoNs::EditTarget t{lv, ids, next};
    UndoNs::RotateCommand cmd(0, 0, 0, +1);
    cmd.Do(t);
    std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 3u);
    cmd.Undo(t);
    idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 2u);
}

TEST(RotateCommandTest, NonExistentCellIsNoOp)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    UndoNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();
    UndoNs::RotateCommand cmd(7, 7, 7, +1);
    cmd.Do(t);
    cmd.Undo(t);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}
