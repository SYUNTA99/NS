#include "Editor/CategoryPalette.h"
#include "Framework/Platform/Input.h"

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;

TEST(CategoryPalette, InitialSlotIsCube)
{
    EditorNs::CategoryPalette palette;
    EXPECT_EQ(palette.ActiveSlot(), 0u);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Cube");
    EXPECT_TRUE(palette.CurrentIsRotatable());
}

TEST(CategoryPalette, OutOfRangeSlotIsIgnored)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(0);
    palette.SetActiveSlot(99);
    EXPECT_EQ(palette.ActiveSlot(), 0u);
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
