#include "Editor/PaletteTemplates.h"
#include "Editor/Undo/PlaceCommand.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;
namespace BlockNs = NS::Game::Blocks;

TEST(PlaceCommandTest, DoAddsBlockEntry)
{
    LevelNs::LevelData lv;
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    EditorNs::PlaceCommand cmd(5, 0, 3, 10, 1);
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
    LevelNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();
    EditorNs::PlaceCommand cmd(5, 0, 3, 10, 1);
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
    LevelNs::EditTarget t{lv, ids, next};
    const auto before = lv.ComputeCrc32();
    EditorNs::PlaceCommand cmd(5, 0, 3, 10, 1);
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
    LevelNs::EditTarget t{lv, ids, next};
    EditorNs::PlaceCommand cmd(0, 0, 0, 1, 5);
    cmd.Do(t);
    const std::size_t idx = LevelNs::FindGridObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::GridRotationStep(lv.objects[idx]), 1u);
}

namespace
{
    LevelNs::ObjectInstance PlaceOneAndTake(EditorNs::PlaceCommand&& cmd) noexcept
    {
        LevelNs::LevelData lv;
        std::vector<std::uint32_t> ids;
        std::uint32_t next = 0;
        LevelNs::EditTarget t{lv, ids, next};
        cmd.Do(t);
        return lv.objects.empty() ? LevelNs::ObjectInstance{} : lv.objects.front();
    }
} // namespace

TEST(PlaceCommandTest, TemplateClonePlacementEqualsLegacyKindPlacement)
{
    struct Case
    {
        std::uint16_t kind;
        std::uint8_t rotation;
    };
    const Case cases[] = {
        {BlockNs::kBlockIdSolid, 1},
        {BlockNs::kBlockIdSlope45, 3},
        {BlockNs::kBlockIdHazard, 0},
    };

    for (const auto& c : cases)
    {
        const LevelNs::ObjectInstance legacy = PlaceOneAndTake(EditorNs::PlaceCommand(7, 2, 4, c.kind, c.rotation));
        const LevelNs::ObjectInstance cloned =
            PlaceOneAndTake(EditorNs::PlaceCommand(EditorNs::PaletteTemplateForKind(c.kind), 7, 2, 4, c.rotation));

        EXPECT_EQ(cloned, legacy);
        EXPECT_EQ(cloned, LevelNs::MakeGridObject(7, 2, 4, c.kind, c.rotation));
    }
}

TEST(PlaceCommandTest, AllPaletteSlotsClonePlacementMatchLegacyKind)
{
    const auto& slots = EditorNs::PaletteTemplateSlots();
    constexpr std::uint8_t kRotation = 2;
    for (const auto& slot : slots)
    {
        const std::uint16_t kind = slot.prototype.kind;
        if (kind == BlockNs::kBlockIdSpawn)
            continue; // spawn は世界に 1 点の marker で grid 配置物にならない

        const LevelNs::ObjectInstance cloned =
            PlaceOneAndTake(EditorNs::PlaceCommand(slot.prototype, 1, 0, -2, kRotation));
        EXPECT_EQ(cloned, LevelNs::MakeGridObject(1, 0, -2, kind, kRotation)) << "kind=" << kind;
    }
}
