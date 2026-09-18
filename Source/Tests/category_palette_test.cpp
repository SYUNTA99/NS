#include "Editor/CategoryPalette.h"
#include "Runtime/Platform/Input.h"

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

TEST(CategoryPalette, KeyboardNumNotEdgeNoChange)
{
    NS::Platform::Input input;
    EditorNs::CategoryPalette palette;

    // OnKeyDown → Update で押した瞬間の判定が消える。 次の TickInput では IsPressed が false
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
    // 配置プレビューのウェッジが prototype の SlopeColliderComponent と同じ 45 度になる
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
