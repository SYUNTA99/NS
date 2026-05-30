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

TEST(CategoryPalette, SlopeSlotCyclesThroughAngles)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(4); // slot 4 = slope (固形/コイン/スター/spawn の次)
    ASSERT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdSlope45);
    palette.CycleActiveVariant();
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdSlope30);
    palette.CycleActiveVariant();
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdSlope22);
    palette.CycleActiveVariant();
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdSlope15);
    palette.CycleActiveVariant();
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdSlope45);
}

TEST(CategoryPalette, CycleVariantOnNonSlopeIsNoOp)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(0); // solid
    palette.CycleActiveVariant();
    EXPECT_EQ(palette.CurrentBlockId(), EditorNs::kBlockIdSolid);
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
