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

TEST(CategoryPalette, SlopeSlotIsRotatableWedge)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(1);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Slope 45");
    EXPECT_TRUE(palette.CurrentIsRotatable());
    // cursor preview の wedge が prototype の SlopeCollider と同じ 45 度を指す
    EXPECT_NEAR(palette.CurrentSlopeAngleDegrees(), 45.0f, 1e-3f);
}

TEST(CategoryPalette, GoalSlotIsNotRotatableAndHasNoSlope)
{
    EditorNs::CategoryPalette palette;
    palette.SetActiveSlot(2);
    EXPECT_STREQ(palette.CurrentTemplateName(), "Goal");
    EXPECT_FALSE(palette.CurrentIsRotatable());
    EXPECT_LT(palette.CurrentSlopeAngleDegrees(), 0.0f);
}
