#include "Framework/Platform/Input.h"
#include "Game/Editor/BlockRegistry.h"
#include "Game/Editor/CategoryPalette.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Game::Editor;

TEST(CategoryPalette, InitialSlotIsSolid)
{
    EditorNs::CategoryPalette palette;
    EXPECT_EQ(palette.ActiveSlot(), 0u);
    EXPECT_EQ(palette.SlotBlockId(0), EditorNs::kBlockIdSolid);
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdSolid);
}

TEST(CategoryPalette, SlotLayoutMatchesVerticalSliceMapping)
{
    EditorNs::CategoryPalette palette;
    EXPECT_EQ(palette.SlotBlockId(0), EditorNs::kBlockIdSolid);
    EXPECT_EQ(palette.SlotBlockId(1), EditorNs::kBlockIdCoin);
    EXPECT_EQ(palette.SlotBlockId(2), EditorNs::kBlockIdPowerStar);
    EXPECT_EQ(palette.SlotBlockId(3), EditorNs::kBlockIdSpawn);
}

TEST(CategoryPalette, SetActiveSlotChangesCurrentBlockId)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(2);
    EXPECT_EQ(palette.ActiveSlot(), 2u);
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdPowerStar);
}

TEST(CategoryPalette, OutOfRangeSlotIsIgnored)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(0);
    palette.SetActiveSlot(99);
    EXPECT_EQ(palette.ActiveSlot(), 0u);
}

TEST(CategoryPalette, OutOfRangeSlotBlockIdReturnsZero)
{
    EditorNs::CategoryPalette palette;
    EXPECT_EQ(palette.SlotBlockId(99), 0u);
}

TEST(CategoryPalette, KeyboardNumSelectsSlot)
{
    NS::Platform::Input input;
    EditorNs::CategoryPalette palette;

    // 数字キー '3' edge → 0-indexed の slot 2 (PowerStar) が active になる
    input.Keyboard().OnKeyDown(NS::Platform::Key::Num3);
    palette.TickInput(&input, nullptr);

    EXPECT_EQ(palette.ActiveSlot(), 2u);
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdPowerStar);
}

TEST(CategoryPalette, KeyboardNumNotEdgeNoChange)
{
    NS::Platform::Input input;
    EditorNs::CategoryPalette palette;

    // OnKeyDown → Update で edge が消費される。 次の TickInput では IsPressed=false
    input.Keyboard().OnKeyDown(NS::Platform::Key::Num4);
    input.Keyboard().Update();
    palette.TickInput(&input, nullptr);

    EXPECT_EQ(palette.ActiveSlot(), 0u);
}
