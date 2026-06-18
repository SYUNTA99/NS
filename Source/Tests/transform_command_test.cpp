#include "Game/Level/LevelData.h"
#include "Game/Undo/EditTarget.h"
#include "Game/Undo/TransformCommand.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace UndoNs = NS::Game::Undo;
namespace LevelNs = NS::Game::Level;

namespace
{
    LevelNs::ObjectInstance MakeFree(float x, float y, float z, std::uint16_t kind = 1)
    {
        LevelNs::ObjectInstance o;
        o.positionX = x;
        o.positionY = y;
        o.positionZ = z;
        o.kind = kind;
        return o;
    }
} // namespace

TEST(TransformCommandTest, DoReplacesWithAfterUndoRestoresBefore)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeFree(0, 0, 0));
    std::vector<std::uint32_t> ids{7};
    std::uint32_t next = 8;
    UndoNs::EditTarget t{lv, ids, next};

    const auto before = lv.objects[0];
    auto after = before;
    after.positionX = 5.0f;

    UndoNs::TransformCommand cmd(7, before, after);
    cmd.Do(t);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 5.0f);
    cmd.Undo(t);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 0.0f);
}

TEST(TransformCommandTest, FlagChangeRoundTripsForPromotion)
{
    LevelNs::LevelData lv;
    LevelNs::ObjectInstance grid = MakeFree(2, 0, 2);
    grid.flags = LevelNs::kObjectFlagGridAligned;
    lv.objects.push_back(grid);
    std::vector<std::uint32_t> ids{3};
    std::uint32_t next = 4;
    UndoNs::EditTarget t{lv, ids, next};

    auto freed = grid;
    freed.flags = static_cast<std::uint8_t>(grid.flags & ~LevelNs::kObjectFlagGridAligned);
    freed.positionX = 6.0f;

    UndoNs::TransformCommand cmd(3, grid, freed);
    cmd.Do(t);
    EXPECT_EQ(lv.objects[0].flags & LevelNs::kObjectFlagGridAligned, 0);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 6.0f);
    cmd.Undo(t);
    EXPECT_EQ(lv.objects[0].flags & LevelNs::kObjectFlagGridAligned, LevelNs::kObjectFlagGridAligned);
    EXPECT_FLOAT_EQ(lv.objects[0].positionX, 2.0f);
}

TEST(TransformCommandTest, ResolvesByIdAfterIndexShift)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeFree(0, 0, 0)); // index 0, id 10
    lv.objects.push_back(MakeFree(1, 1, 1)); // index 1, id 11
    std::vector<std::uint32_t> ids{10, 11};
    std::uint32_t next = 12;
    UndoNs::EditTarget t{lv, ids, next};

    const auto before = lv.objects[1];
    auto after = before;
    after.positionY = 9.0f;
    UndoNs::TransformCommand cmd(11, before, after);

    // 先頭を削って添字を 1 つずらす。 id 11 は index 0 へ移る
    lv.objects.erase(lv.objects.begin());
    ids.erase(ids.begin());

    cmd.Do(t);
    const std::size_t idx = UndoNs::IndexOfId(t, 11);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_FLOAT_EQ(lv.objects[idx].positionY, 9.0f);
}

TEST(TransformCommandTest, MissingIdIsNoOp)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeFree(0, 0, 0));
    std::vector<std::uint32_t> ids{1};
    std::uint32_t next = 2;
    UndoNs::EditTarget t{lv, ids, next};

    const auto crc = lv.ComputeCrc32();
    UndoNs::TransformCommand cmd(999, lv.objects[0], MakeFree(5, 5, 5));
    cmd.Do(t);
    cmd.Undo(t);
    EXPECT_EQ(lv.ComputeCrc32(), crc);
}
