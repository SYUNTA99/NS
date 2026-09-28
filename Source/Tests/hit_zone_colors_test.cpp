#include <Editor/HitZoneColors.h>
#include <Game/Level/HitTier.h>
#include <Runtime/Core/Math.h>

#include <gtest/gtest.h>

namespace
{
    using NS::Game::Level::HitTier;

    bool SameColor(const NS::Core::Color& a, const NS::Core::Color& b)
    {
        return a.R() == b.R() && a.G() == b.G() && a.B() == b.B() && a.A() == b.A();
    }
} // namespace

// 段ごとに色が違い、警告の色はどの段とも違う
TEST(HitZoneColorsTest, EachTierAndTheWarningHaveTheirOwnColor)
{
    const NS::Core::Color center = NS::Editor::HitZoneColor(HitTier::Center);
    const NS::Core::Color nearMiss = NS::Editor::HitZoneColor(HitTier::Near);
    const NS::Core::Color wide = NS::Editor::HitZoneColor(HitTier::Wide);
    const NS::Core::Color warning = NS::Editor::HitZoneWarningColor();
    const NS::Core::Color candidate = NS::Editor::HitZoneCandidateColor();

    EXPECT_FALSE(SameColor(center, nearMiss));
    EXPECT_FALSE(SameColor(center, wide));
    EXPECT_FALSE(SameColor(nearMiss, wide));
    EXPECT_FALSE(SameColor(warning, center));
    EXPECT_FALSE(SameColor(warning, nearMiss));
    EXPECT_FALSE(SameColor(warning, wide));
    EXPECT_FALSE(SameColor(candidate, center));
    EXPECT_FALSE(SameColor(candidate, nearMiss));
    EXPECT_FALSE(SameColor(candidate, wide));
    EXPECT_FALSE(SameColor(candidate, warning));
}

// 範囲の円は段の色。大きさの順が崩れていれば警告の色
TEST(HitZoneColorsTest, RingTurnsToTheWarningColorWhenTheOrderIsBroken)
{
    EXPECT_TRUE(
        SameColor(NS::Editor::HitZoneRingColor(HitTier::Center, false), NS::Editor::HitZoneColor(HitTier::Center)));
    EXPECT_TRUE(SameColor(NS::Editor::HitZoneRingColor(HitTier::Near, false), NS::Editor::HitZoneColor(HitTier::Near)));
    EXPECT_TRUE(SameColor(NS::Editor::HitZoneRingColor(HitTier::Center, true), NS::Editor::HitZoneWarningColor()));
    EXPECT_TRUE(SameColor(NS::Editor::HitZoneRingColor(HitTier::Near, true), NS::Editor::HitZoneWarningColor()));
}
