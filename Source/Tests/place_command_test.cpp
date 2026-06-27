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
    EditorNs::PlaceCommand cmd(LevelNs::MakeGridObject(0, 0, 0, 10, 0), 5, 0, 3, 1);
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
    EditorNs::PlaceCommand cmd(LevelNs::MakeGridObject(0, 0, 0, 10, 0), 5, 0, 3, 1);
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
    EditorNs::PlaceCommand cmd(LevelNs::MakeGridObject(0, 0, 0, 10, 0), 5, 0, 3, 1);
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
    EditorNs::PlaceCommand cmd(LevelNs::MakeGridObject(0, 0, 0, 1, 0), 0, 0, 0, 5);
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

TEST(PlaceCommandTest, TemplateClonePlacesPrototypeWithBakedTransform)
{
    const std::uint16_t kinds[] = {BlockNs::kBlockIdSolid, BlockNs::kBlockIdSlope45, BlockNs::kBlockIdHazard};
    constexpr std::int16_t cx = 7;
    constexpr std::int16_t cy = 2;
    constexpr std::int16_t cz = 4;
    constexpr std::uint8_t rotation = 1;

    for (const std::uint16_t kind : kinds)
    {
        const EditorNs::PaletteTemplate tmpl = EditorNs::PaletteTemplateForKind(kind);

        // 配置結果はテンプレ複製に cell 座標 + 回転 step を焼いたものになる
        LevelNs::ObjectInstance expected = tmpl.prototype;
        expected.positionX = static_cast<float>(cx);
        expected.positionY = static_cast<float>(cy);
        expected.positionZ = static_cast<float>(cz);
        LevelNs::SetGridRotationStep(expected, rotation);

        const LevelNs::ObjectInstance cloned =
            PlaceOneAndTake(EditorNs::PlaceCommand(tmpl.prototype, cx, cy, cz, rotation));
        EXPECT_EQ(cloned, expected) << "kind=" << kind;
    }
}

TEST(PlaceCommandTest, AllPaletteSlotsClonePlacesPrototype)
{
    const auto& slots = EditorNs::PaletteTemplateSlots();
    constexpr std::int16_t cx = 1;
    constexpr std::int16_t cy = 0;
    constexpr std::int16_t cz = -2;
    constexpr std::uint8_t kRotation = 2;
    for (const auto& slot : slots)
    {
        if (slot.isSpawn)
            continue; // spawn は世界に 1 点の marker で grid 配置物にならない

        LevelNs::ObjectInstance expected = slot.prototype;
        expected.positionX = static_cast<float>(cx);
        expected.positionY = static_cast<float>(cy);
        expected.positionZ = static_cast<float>(cz);
        LevelNs::SetGridRotationStep(expected, kRotation);

        const LevelNs::ObjectInstance cloned =
            PlaceOneAndTake(EditorNs::PlaceCommand(slot.prototype, cx, cy, cz, kRotation));
        EXPECT_EQ(cloned, expected) << "slot=" << slot.name;
    }
}
