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
    ASSERT_EQ(lv.objects.size(), 1u);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::ObjectCellX(lv.objects[idx]), 5);
    EXPECT_EQ(LevelNs::ObjectCellY(lv.objects[idx]), 0);
    EXPECT_EQ(LevelNs::ObjectCellZ(lv.objects[idx]), 3);
    EXPECT_EQ(lv.objects[idx].kind, 10u);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
}

TEST(PlaceCommandTest, UndoRestoresEmptyState)
{
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.objects.empty());
}

TEST(PlaceCommandTest, ReplaceExistingBlockPreservesUndoRestore)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(5, 0, 3, 20, 2));
    const auto before = lv.ComputeCrc32();
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(lv);
    std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(lv.objects[idx].kind, 10u);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
    cmd.Undo(lv);
    idx = LevelNs::FindGridObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(lv.objects[idx].kind, 20u);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 2u);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}

TEST(PlaceCommandTest, RotationIsMaskedToTwoBits)
{
    LevelNs::LevelData lv;
    UndoNs::PlaceCommand cmd(0, 0, 0, 1, 5); // 5 & 3 == 1
    cmd.Do(lv);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
}
