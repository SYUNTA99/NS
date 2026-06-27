#include "Editor/CategoryPalette.h"
#include "Framework/Platform/Input.h"
#include "Game/Blocks/BlockRegistry.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;

TEST(CategoryPalette, InitialSlotIsSolid)
{
    EditorNs::CategoryPalette palette;
    EXPECT_EQ(palette.ActiveSlot(), 0u);
    EXPECT_EQ(palette.SlotBlockId(0), NS::Game::Blocks::kBlockIdSolid);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Solid");
    EXPECT_FALSE(palette.CurrentIsSpawn());
    EXPECT_TRUE(palette.CurrentIsRotatable());
}

TEST(CategoryPalette, SlotLayoutMatchesVerticalSliceMapping)
{
    EditorNs::CategoryPalette palette;
    EXPECT_EQ(palette.SlotBlockId(0), NS::Game::Blocks::kBlockIdSolid);
    EXPECT_EQ(palette.SlotBlockId(1), NS::Game::Blocks::kBlockIdCoin);
    EXPECT_EQ(palette.SlotBlockId(2), NS::Game::Blocks::kBlockIdPowerStar);
    EXPECT_EQ(palette.SlotBlockId(3), NS::Game::Blocks::kBlockIdSpawn);
}

TEST(CategoryPalette, SetActiveSlotChangesCurrentTemplate)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(2);
    EXPECT_EQ(palette.ActiveSlot(), 2u);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Star");
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

TEST(CategoryPalette, SpawnSlotIsSpawnAndNonRotatable)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(3); // spawn
    EXPECT_TRUE(palette.CurrentIsSpawn());
    EXPECT_FALSE(palette.CurrentIsRotatable());
}

TEST(CategoryPalette, KeyboardNumSelectsSlot)
{
    NS::Platform::Input input;
    EditorNs::CategoryPalette palette;

    // 数字キー '3' edge → 0-indexed の slot 2 (PowerStar) が active になる
    input.Keyboard().OnKeyDown(NS::Platform::Key::Num3);
    palette.TickInput(&input, nullptr);

    EXPECT_EQ(palette.ActiveSlot(), 2u);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Star");
}

TEST(CategoryPalette, SlopeSlotCyclesThroughAngles)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(4); // slot 4 = slope (固形/コイン/スター/spawn の次)
    ASSERT_FLOAT_EQ(palette.CurrentSlopeAngleDegrees(), 45.0f);
    palette.CycleActiveVariant();
    EXPECT_FLOAT_EQ(palette.CurrentSlopeAngleDegrees(), 30.0f);
    palette.CycleActiveVariant();
    EXPECT_FLOAT_EQ(palette.CurrentSlopeAngleDegrees(), 22.5f);
    palette.CycleActiveVariant();
    EXPECT_FLOAT_EQ(palette.CurrentSlopeAngleDegrees(), 15.0f);
    palette.CycleActiveVariant();
    EXPECT_FLOAT_EQ(palette.CurrentSlopeAngleDegrees(), 45.0f);
}

TEST(CategoryPalette, CycleVariantOnNonSlopeIsNoOp)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(0); // solid
    palette.CycleActiveVariant();
    EXPECT_STREQ(palette.CurrentTemplateName(), "Solid");
    EXPECT_LT(palette.CurrentSlopeAngleDegrees(), 0.0f);
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
