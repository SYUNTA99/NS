#include "Game/Level/LevelData.h"
#include "Editor/Undo/DeleteCommand.h"
#include "Game/Level/EditTarget.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

TEST(DeleteCommandTest, DoRemovesExistingBlock)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, 5, 2));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    LevelNs::EditTarget t{lv, ids, next};
    EditorNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(t);
    EXPECT_TRUE(lv.objects.empty());
    EXPECT_TRUE(ids.empty());
}

TEST(DeleteCommandTest, UndoRestoresOriginalEntry)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeGridObject(2, 0, 4, 5, 2));
    std::vector<std::uint32_t> ids{0};
    std::uint32_t next = 1;
    LevelNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();
    EditorNs::DeleteCommand cmd(2, 0, 4);
    cmd.Do(t);
    cmd.Undo(t);
    ASSERT_EQ(lv.objects.size(), 1u);
    EXPECT_EQ(ids.size(), 1u);
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
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();
    EditorNs::DeleteCommand cmd(99, 99, 99);
    cmd.Do(t);
    cmd.Undo(t);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.objects.empty());
}
