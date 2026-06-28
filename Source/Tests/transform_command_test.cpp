#include "Editor/Undo/SetSpawnCommand.h"
#include "Editor/Undo/TransformCommand.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;

namespace
{
    LevelNs::ObjectInstance MakeFree(float x, float y, float z)
    {
        LevelNs::ObjectInstance o;
        o.positionX = x;
        o.positionY = y;
        o.positionZ = z;
        return o;
    }
} // namespace

TEST(TransformCommandTest, DoReplacesWithAfterUndoRestoresBefore)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeFree(0, 0, 0));
    std::vector<std::uint32_t> ids{7};
    std::uint32_t next = 8;
    LevelNs::EditTarget t{lv, ids, next};

    const auto before = lv.objects[0];
    auto after = before;
    after.positionX = 5.0f;

    EditorNs::TransformCommand cmd(7, before, after);
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
    LevelNs::EditTarget t{lv, ids, next};

    auto freed = grid;
    freed.flags = static_cast<std::uint8_t>(grid.flags & ~LevelNs::kObjectFlagGridAligned);
    freed.positionX = 6.0f;

    EditorNs::TransformCommand cmd(3, grid, freed);
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
    LevelNs::EditTarget t{lv, ids, next};

    const auto before = lv.objects[1];
    auto after = before;
    after.positionY = 9.0f;
    EditorNs::TransformCommand cmd(11, before, after);

    // 先頭を削って添字を 1 つずらす。 id 11 は index 0 へ移る
    lv.objects.erase(lv.objects.begin());
    ids.erase(ids.begin());

    cmd.Do(t);
    const std::size_t idx = LevelNs::IndexOfId(t, 11);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_FLOAT_EQ(lv.objects[idx].positionY, 9.0f);
}

TEST(TransformCommandTest, MissingIdIsNoOp)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeFree(0, 0, 0));
    std::vector<std::uint32_t> ids{1};
    std::uint32_t next = 2;
    LevelNs::EditTarget t{lv, ids, next};

    const auto crc = lv.ComputeCrc32();
    EditorNs::TransformCommand cmd(999, lv.objects[0], MakeFree(5, 5, 5));
    cmd.Do(t);
    cmd.Undo(t);
    EXPECT_EQ(lv.ComputeCrc32(), crc);
}

TEST(SetSpawnCommandTest, DoAppliesAfterUndoRestoresBefore)
{
    LevelNs::LevelData lv;
    lv.spawnX = 1.0f;
    lv.spawnY = 2.0f;
    lv.spawnZ = 3.0f;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};

    const EditorNs::SetSpawnCommand::SpawnState before{1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const EditorNs::SetSpawnCommand::SpawnState after{4.5f, 6.0f, -7.0f, 0.0f, 0.70710677f, 0.0f, 0.70710677f};

    EditorNs::SetSpawnCommand cmd(before, after);
    cmd.Do(t);
    EXPECT_FLOAT_EQ(lv.spawnX, 4.5f);
    EXPECT_FLOAT_EQ(lv.spawnY, 6.0f);
    EXPECT_FLOAT_EQ(lv.spawnZ, -7.0f);
    EXPECT_FLOAT_EQ(lv.spawnRotationY, 0.70710677f);
    EXPECT_FLOAT_EQ(lv.spawnRotationW, 0.70710677f);

    cmd.Undo(t);
    EXPECT_FLOAT_EQ(lv.spawnX, 1.0f);
    EXPECT_FLOAT_EQ(lv.spawnY, 2.0f);
    EXPECT_FLOAT_EQ(lv.spawnZ, 3.0f);
    EXPECT_FLOAT_EQ(lv.spawnRotationY, 0.0f);
    EXPECT_FLOAT_EQ(lv.spawnRotationW, 1.0f);
}
