#include "Editor/CategoryPalette.h"
#include "Framework/Platform/Input.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;

TEST(CategoryPalette, InitialSlotIsCube)
{
    EditorNs::CategoryPalette palette;
    EXPECT_EQ(palette.ActiveSlot(), 0u);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Cube");
    EXPECT_FALSE(palette.CurrentIsSpawn());
    EXPECT_TRUE(palette.CurrentIsRotatable());
}

TEST(CategoryPalette, SecondSlotIsSpawn)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(1);
    EXPECT_EQ(palette.ActiveSlot(), 1u);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Spawn");
    EXPECT_TRUE(palette.CurrentIsSpawn());
    EXPECT_FALSE(palette.CurrentIsRotatable());
}

TEST(CategoryPalette, OutOfRangeSlotIsIgnored)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(0);
    palette.SetActiveSlot(99);
    EXPECT_EQ(palette.ActiveSlot(), 0u);
}

TEST(CategoryPalette, KeyboardNumSelectsSlot)
{
    NS::Platform::Input input;
    EditorNs::CategoryPalette palette;

    // 数字キー '2' edge で spawn の slot 1 が active になる
    input.Keyboard().OnKeyDown(NS::Platform::Key::Num2);
    palette.TickInput(&input, nullptr);

    EXPECT_EQ(palette.ActiveSlot(), 1u);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Spawn");
}

TEST(CategoryPalette, CycleVariantIsNoOp)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(0);
    palette.CycleActiveVariant();
    EXPECT_STREQ(palette.CurrentTemplateName(), "Cube");
    EXPECT_LT(palette.CurrentSlopeAngleDegrees(), 0.0f);
}

TEST(CategoryPalette, KeyboardNumNotEdgeNoChange)
{
    NS::Platform::Input input;
    EditorNs::CategoryPalette palette;

    // OnKeyDown → Update で edge が消費される。 次の TickInput では IsPressed が false
    input.Keyboard().OnKeyDown(NS::Platform::Key::Num2);
    input.Keyboard().Update();
    palette.TickInput(&input, nullptr);

    EXPECT_EQ(palette.ActiveSlot(), 0u);
}
