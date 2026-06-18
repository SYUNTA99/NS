#include "Game/Level/LevelData.h"
#include "Game/Undo/EditTarget.h"
#include "Game/Undo/PlaceCommand.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

TEST(PlaceCommandTest, DoAddsBlockEntry)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    UndoNs::EditTarget t{lv, ids, next};
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(t);
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(ids.size(), lv.objects.size());
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
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    UndoNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(t);
    cmd.Undo(t);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.objects.empty());
    EXPECT_TRUE(ids.empty());
}

TEST(PlaceCommandTest, ReplaceExistingBlockPreservesUndoRestore)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(5, 0, 3, 20, 2));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    UndoNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();
    UndoNs::PlaceCommand cmd(5, 0, 3, 10, 1);
    cmd.Do(t);
    std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(lv.objects[idx].kind, 10u);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
    EXPECT_EQ(ids.size(), lv.objects.size());
    cmd.Undo(t);
    idx = LevelNs::FindGridObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(lv.objects[idx].kind, 20u);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 2u);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}

TEST(PlaceCommandTest, RotationIsMaskedToTwoBits)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    UndoNs::EditTarget t{lv, ids, next};
    UndoNs::PlaceCommand cmd(0, 0, 0, 1, 5);
    cmd.Do(t);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
}
