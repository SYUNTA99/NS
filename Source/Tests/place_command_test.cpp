#include "Editor/PaletteTemplates.h"
#include "Editor/Undo/PlaceCommand.h"
#include "GameCore/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::GameCore::Level;

TEST(PlaceCommandTest, DoAddsGridObject)
{
    LevelNs::LevelData lv;
    EditorNs::PlaceCommand cmd(LevelNs::MakeCellObject(0, 0, 0, 0), 5, 0, 3, 1);
    cmd.Do(lv);
    ASSERT_EQ(lv.objects.size(), 1u);
    const std::size_t idx = LevelNs::FindObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::ObjectCellX(lv.objects[idx]), 5);
    EXPECT_EQ(LevelNs::ObjectCellY(lv.objects[idx]), 0);
    EXPECT_EQ(LevelNs::ObjectCellZ(lv.objects[idx]), 3);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 1u);
}

TEST(PlaceCommandTest, UndoRestoresEmptyState)
{
    LevelNs::LevelData lv;
    const auto before = lv.ComputeCrc32();
    EditorNs::PlaceCommand cmd(LevelNs::MakeCellObject(0, 0, 0, 0), 5, 0, 3, 1);
    cmd.Do(lv);
    cmd.Undo(lv);
    EXPECT_EQ(lv.ComputeCrc32(), before);
    EXPECT_TRUE(lv.objects.empty());
}

TEST(PlaceCommandTest, ReplaceExistingBlockPreservesUndoRestore)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakeCellObject(5, 0, 3, 2));
    const auto before = lv.ComputeCrc32();
    EditorNs::PlaceCommand cmd(LevelNs::MakeCellObject(0, 0, 0, 0), 5, 0, 3, 1);
    cmd.Do(lv);
    std::size_t idx = LevelNs::FindObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 1u);
    cmd.Undo(lv);
    idx = LevelNs::FindObjectAtCell(lv, 5, 0, 3);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 2u);
    EXPECT_EQ(lv.ComputeCrc32(), before);
}

TEST(PlaceCommandTest, RotationIsMaskedToTwoBits)
{
    LevelNs::LevelData lv;
    EditorNs::PlaceCommand cmd(LevelNs::MakeCellObject(0, 0, 0, 0), 0, 0, 0, 5);
    cmd.Do(lv);
    const std::size_t idx = LevelNs::FindObjectAtCell(lv, 0, 0, 0);
    ASSERT_NE(idx, LevelNs::kNoObjectIndex);
    EXPECT_EQ(LevelNs::CellRotationStep(lv.objects[idx]), 1u);
}

namespace
{
    LevelNs::ObjectInstance PlaceOneAndTake(EditorNs::PlaceCommand&& cmd) noexcept
    {
        LevelNs::LevelData lv;
        cmd.Do(lv);
        return lv.objects.empty() ? LevelNs::ObjectInstance{} : lv.objects.front();
    }
} // namespace

TEST(PlaceCommandTest, TemplateClonePlacesPrototypeWithBakedTransform)
{
    constexpr std::int16_t cx = 7;
    constexpr std::int16_t cy = 2;
    constexpr std::int16_t cz = 4;
    constexpr std::uint8_t rotation = 1;

    const EditorNs::PaletteTemplate tmpl = EditorNs::PaletteTemplateSlots()[0];

    // 配置結果はテンプレ複製に cell 座標 + 回転 step を焼いたものになる
    LevelNs::ObjectInstance expected = tmpl.prototype;
    expected.positionX = static_cast<float>(cx);
    expected.positionY = static_cast<float>(cy);
    expected.positionZ = static_cast<float>(cz);
    LevelNs::SetCellRotationStep(expected, rotation);

    const LevelNs::ObjectInstance cloned =
        PlaceOneAndTake(EditorNs::PlaceCommand(tmpl.prototype, cx, cy, cz, rotation));
    // 配置時に永続 id が採番されるため、 id を揃えた上で残り全メンバの複製一致を確かめる
    EXPECT_NE(cloned.objectId, 0u);
    expected.objectId = cloned.objectId;
    EXPECT_EQ(cloned, expected);
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
        LevelNs::ObjectInstance expected = slot.prototype;
        expected.positionX = static_cast<float>(cx);
        expected.positionY = static_cast<float>(cy);
        expected.positionZ = static_cast<float>(cz);
        LevelNs::SetCellRotationStep(expected, kRotation);

        const LevelNs::ObjectInstance cloned =
            PlaceOneAndTake(EditorNs::PlaceCommand(slot.prototype, cx, cy, cz, kRotation));
        EXPECT_NE(cloned.objectId, 0u) << "slot=" << slot.name;
        expected.objectId = cloned.objectId;
        EXPECT_EQ(cloned, expected) << "slot=" << slot.name;
    }
}
